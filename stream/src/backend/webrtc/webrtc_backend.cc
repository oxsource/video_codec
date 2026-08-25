#include "src/backend/webrtc/webrtc_backend.h"
#include "src/api/stream_config.h"
#include "src/core/video_stream_register.h"

#include "src/framework/core/status.h"
#include "src/framework/core/types.h"

#include <rtc/rtc.hpp>

#include <sstream>

namespace video {
namespace stream {
namespace {

std::vector<WhipIceCandidate> ExtractIceCandidates(const std::string& sdp) {
  std::vector<WhipIceCandidate> candidates;
  std::istringstream stream(sdp);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.find("a=candidate:") == 0) {
      WhipIceCandidate cand;
      cand.candidate = line.substr(12);
      candidates.push_back(cand);
    }
  }
  return candidates;
}

}  // namespace

WebrtcBackend::WebrtcBackend(const StreamConfig& config) : config_(config) {
  whip_session_ = std::make_unique<WhipSession>();
  rtc::InitLogger(rtc::LogLevel::Warning);
}

WebrtcBackend::~WebrtcBackend() {
  Disconnect();
}

void WebrtcBackend::OnStateChange(rtc::PeerConnection::State state) {
  std::printf("  [webrtc] state change: %d\n", static_cast<int>(state));
  switch (state) {
    case rtc::PeerConnection::State::New:
      break;
    case rtc::PeerConnection::State::Connecting:
      status_.state = StreamState::kConnecting;
      break;
    case rtc::PeerConnection::State::Connected:
      connected_ = true;
      status_.state = StreamState::kStreaming;
      {
        std::lock_guard<std::mutex> lock(mtx_);
        connected_state_ = true;
      }
      cv_.notify_one();
      break;
    case rtc::PeerConnection::State::Disconnected:
    case rtc::PeerConnection::State::Failed:
      connected_ = false;
      status_.state = StreamState::kDisconnected;
      break;
    case rtc::PeerConnection::State::Closed:
      connected_ = false;
      status_.state = StreamState::kDestroyed;
      break;
  }
  if (callback_) callback_(status_);
}

video::codec::Status WebrtcBackend::Connect(const StreamConfig& config) {
  config_ = config;
  std::printf("  [webrtc] Connect: url=%s\n", config_.remote_url.c_str());

  if (config_.remote_url.empty()) {
    return video::codec::Status::kInvalidArgument;
  }

  std::string whip_endpoint = config_.remote_url;
  if (whip_endpoint.back() == '/') {
    whip_endpoint.pop_back();
  }

  rtc::Configuration pc_config;
  pc_config.iceServers.emplace_back("stun:stun.l.google.com:19302");
  std::printf("  [webrtc] creating PeerConnection\n");

  pc_ = std::make_shared<rtc::PeerConnection>(pc_config);

  pc_->onStateChange([this](rtc::PeerConnection::State state) {
    OnStateChange(state);
  });

  pc_->onLocalDescription([this, whip_endpoint](const rtc::Description& description) {
    std::string offer = std::string(description);
    std::printf("  [webrtc] onLocalDescription: %zu bytes, offer_ready_=%d, gathering=%d\n",
                offer.size(), offer_ready_, gathering_complete_);

    if (offer_ready_) {
      std::printf("  [webrtc]   -> already sent, ignoring\n");
      return;
    }

    if (!gathering_complete_) {
      current_offer_ = offer;
      std::printf("  [webrtc]   -> waiting for ICE gathering (%zu bytes stored)\n", offer.size());
      return;
    }

    std::printf("  [webrtc]   -> gathering done, sending WHIP POST\n");
    current_offer_ = offer;

    {
      std::lock_guard<std::mutex> lock(mtx_);
      offer_ready_ = true;
    }
    cv_.notify_one();

    whip_session_->SetOnReady([this](const std::string& sdp) {
      std::printf("  [webrtc] WHIP OnReady, setting remote description\n");
      pc_->setRemoteDescription(sdp);
      session_id_ = whip_session_->SessionId();
      {
        std::lock_guard<std::mutex> lock(mtx_);
        answer_ready_ = true;
      }
      cv_.notify_one();
    });

    whip_session_->SetOnError([this](const std::string& error) {
      std::printf("  [webrtc] WHIP OnError: %s\n", error.c_str());
      status_.last_error = error;
      {
        std::lock_guard<std::mutex> lock(mtx_);
        answer_ready_ = true;
        connect_result_ = video::codec::Status::kEncodeFailed;
      }
      cv_.notify_one();
    });

    if (!whip_session_->Create(whip_endpoint, offer)) {
      std::printf("  [webrtc] WHIP Create failed\n");
      {
        std::lock_guard<std::mutex> lock(mtx_);
        answer_ready_ = true;
        connect_result_ = video::codec::Status::kEncodeFailed;
      }
      cv_.notify_one();
    }
  });

  pc_->onGatheringStateChange([this, whip_endpoint](rtc::PeerConnection::GatheringState state) {
    std::printf("  [webrtc] onGatheringStateChange: %d, offer_ready_=%d\n",
                static_cast<int>(state), offer_ready_);
    if (state == rtc::PeerConnection::GatheringState::Complete) {
      gathering_complete_ = true;
      if (!offer_ready_) {
        auto local_desc = pc_->localDescription();
        if (!local_desc) {
          std::printf("  [webrtc]   -> no local description available\n");
          return;
        }
        std::string offer = std::string(*local_desc);
        std::printf("  [webrtc]   -> gathering complete, SDP=%zu bytes\n", offer.size());
        auto candidates = ExtractIceCandidates(offer);
        std::printf("  [webrtc]   -> %zu ICE candidates in SDP\n", candidates.size());

        whip_session_->SetOnReady([this](const std::string& sdp) {
          std::printf("  [webrtc] WHIP OnReady (from gathering), setting remote description\n");
          pc_->setRemoteDescription(sdp);
          session_id_ = whip_session_->SessionId();
          {
            std::lock_guard<std::mutex> lock(mtx_);
            answer_ready_ = true;
          }
          cv_.notify_one();
        });

        whip_session_->SetOnError([this](const std::string& error) {
          std::printf("  [webrtc] WHIP OnError (from gathering): %s\n", error.c_str());
          status_.last_error = error;
          {
            std::lock_guard<std::mutex> lock(mtx_);
            answer_ready_ = true;
            connect_result_ = video::codec::Status::kEncodeFailed;
          }
          cv_.notify_one();
        });

        offer_ready_ = true;
        if (!whip_session_->Create(whip_endpoint, offer)) {
          std::printf("  [webrtc] WHIP Create (from gathering) failed\n");
          std::lock_guard<std::mutex> lock(mtx_);
          answer_ready_ = true;
          connect_result_ = video::codec::Status::kEncodeFailed;
          cv_.notify_one();
        }
      }
    }
  });

  const rtc::SSRC ssrc = 42;
  rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
  media.addH264Codec(96);
  media.addSSRC(ssrc, "video-send");
  video_track_ = pc_->addTrack(media);

  {
    rtp_config_ = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, "video-send", 96, 90000);
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::NalUnit::Separator::StartSequence, rtp_config_);
    // Add RTCP handlers for keyframe request (PLI) and packet retransmission (NACK).
    packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(rtp_config_));
    packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
    video_track_->setMediaHandler(packetizer);
  }

  pc_->setLocalDescription();

  {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait_for(lock, std::chrono::seconds(10), [this] { return answer_ready_; });
  }

  if (connect_result_ != video::codec::Status::kOk) {
    return connect_result_;
  }

  std::printf("  [webrtc] waiting for ICE connected state...\n");
  {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait_for(lock, std::chrono::seconds(10), [this] { return connected_state_; });
  }
  // Don't check connected_state_ here — MediaMTX (and other servers) may start
  // the DTLS handshake and hit their "no tracks" timeout before the Connected
  // state fires. The caller will push data immediately; if the track is still
  // closed by then SendVideo will report the error naturally.

  std::printf("  [webrtc] Connect complete, connected=%d\n", connected_);
  return connect_result_;
}

video::codec::Status WebrtcBackend::SendVideo(const video::codec::VideoPacket& packet) {
  if (!video_track_) return video::codec::Status::kNotInitialized;
  // Advance the RTP timestamp (90 kHz clock) from the encoder PTS. Without this
  // every packet carries the same timestamp and the receiver never emits a frame.
  if (rtp_config_) {
    uint32_t new_ts;
    if (packet.pts_us > 0) {
      new_ts = rtp_config_->startTimestamp +
               static_cast<uint32_t>((static_cast<uint64_t>(packet.pts_us) * 90) / 1000);
    } else {
      // Fallback: encoder didn't set PTS, increment by 3000 per frame (= 30fps @ 90kHz)
      new_ts = rtp_config_->startTimestamp +
               static_cast<uint32_t>(frame_index_ * 3000);
    }
    rtp_config_->timestamp = new_ts;
    frame_index_++;
  }
  std::printf("  [webrtc] SendVideo: %zu bytes, ts=%u\n", packet.data.size(), rtp_config_ ? rtp_config_->timestamp : 0);
  try {
    rtc::message_variant msg(std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(packet.data.data()),
        reinterpret_cast<const std::byte*>(packet.data.data() + packet.data.size())));
    video_track_->send(msg);
    stats_.packets_sent++;
    stats_.bytes_sent += packet.data.size();
    std::printf("  [webrtc] SendVideo OK (%llu total)\n", (unsigned long long)stats_.packets_sent);
  } catch (const std::exception& e) {
    std::printf("  [webrtc] SendVideo FAILED: %s\n", e.what());
    status_.last_error = e.what();
    return video::codec::Status::kEncodeFailed;
  }
  return video::codec::Status::kOk;
}

video::codec::Status WebrtcBackend::SendAudio(const video::codec::AudioPacket& packet) {
  if (!connected_) return video::codec::Status::kNotInitialized;
  stats_.packets_sent++;
  stats_.bytes_sent += packet.data.size();
  return video::codec::Status::kOk;
}

video::codec::Status WebrtcBackend::Disconnect() {
  std::printf("  [webrtc] Disconnect() called\n");
  if (pc_) {
    pc_->close();
    pc_.reset();
  }
  video_track_.reset();
  connected_ = false;
  status_.state = StreamState::kDisconnected;
  if (callback_) callback_(status_);
  return video::codec::Status::kOk;
}

StreamStats WebrtcBackend::GetStats() {
  return stats_;
}

void WebrtcBackend::SetStatusCallback(StatusCallback callback) {
  callback_ = std::move(callback);
}

std::unique_ptr<StreamBackend> WebrtcBackend::Create(const StreamConfig& config) {
  return std::make_unique<WebrtcBackend>(config);
}

VIDEO_STREAM_REGISTER(video::stream::kBackendWebRTC, WebrtcBackend::Create);

}  // namespace stream
}  // namespace video