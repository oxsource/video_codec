#include "src/backend/webrtc/webrtc_backend.h"
#include "src/api/stream_config.h"
#include "src/core/video_stream_register.h"

#include "src/framework/core/log_slot.h"
#include "src/framework/core/status.h"
#include "src/framework/core/types.h"

#include <rtc/rtc.hpp>

#include <sstream>
#include <string>

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
  whip_session_ = std::make_unique<WhipSession>(config_.network);
  rtc::InitLogger(rtc::LogLevel::Warning);
}

WebrtcBackend::~WebrtcBackend() {
  Disconnect();
}

void WebrtcBackend::OnStateChange(rtc::PeerConnection::State state) {
  VC_LOG(video::codec::LogLevel::kDebug, std::string("webrtc state change: ") + std::to_string(static_cast<int>(state)));
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
  VC_LOG(video::codec::LogLevel::kDebug, std::string("Connect: url=") + config_.remote_url);

  if (config_.remote_url.empty()) {
    return video::codec::Status::kInvalidArgument;
  }

  std::string whip_endpoint = config_.remote_url;
  if (whip_endpoint.back() == '/') {
    whip_endpoint.pop_back();
  }

  rtc::Configuration pc_config;
  pc_config.iceServers.emplace_back("stun:stun.l.google.com:19302");
  VC_LOG(video::codec::LogLevel::kDebug, "creating PeerConnection");

  pc_ = std::make_shared<rtc::PeerConnection>(pc_config);

  pc_->onStateChange([this](rtc::PeerConnection::State state) {
    OnStateChange(state);
  });

  pc_->onLocalDescription([this, whip_endpoint](const rtc::Description& description) {
    std::string offer = std::string(description);
    VC_LOG(video::codec::LogLevel::kDebug,
           std::string("onLocalDescription: ") + std::to_string(offer.size()) +
           " bytes, offer_ready_=" + std::to_string(offer_ready_) +
           ", gathering=" + std::to_string(gathering_complete_));

    if (offer_ready_) {
      VC_LOG(video::codec::LogLevel::kDebug, "  -> already sent, ignoring");
      return;
    }

    if (!gathering_complete_) {
      current_offer_ = offer;
      VC_LOG(video::codec::LogLevel::kDebug,
             std::string("  -> waiting for ICE gathering (") + std::to_string(offer.size()) + " bytes stored)");
      return;
    }

    VC_LOG(video::codec::LogLevel::kDebug, "  -> gathering done, sending WHIP POST");
    current_offer_ = offer;

    {
      std::lock_guard<std::mutex> lock(mtx_);
      offer_ready_ = true;
    }
    cv_.notify_one();

    whip_session_->SetOnReady([this](const std::string& sdp) {
      VC_LOG(video::codec::LogLevel::kDebug, "WHIP OnReady, setting remote description");
      pc_->setRemoteDescription(sdp);
      session_id_ = whip_session_->SessionId();
      {
        std::lock_guard<std::mutex> lock(mtx_);
        answer_ready_ = true;
      }
      cv_.notify_one();
    });

    whip_session_->SetOnError([this](const std::string& error) {
      VC_LOG(video::codec::LogLevel::kError, std::string("WHIP OnError: ") + error);
      status_.last_error = error;
      {
        std::lock_guard<std::mutex> lock(mtx_);
        answer_ready_ = true;
        connect_result_ = video::codec::Status::kEncodeFailed;
      }
      cv_.notify_one();
    });

    if (!whip_session_->Create(whip_endpoint, offer)) {
      VC_LOG(video::codec::LogLevel::kError, "WHIP Create failed");
      {
        std::lock_guard<std::mutex> lock(mtx_);
        answer_ready_ = true;
        connect_result_ = video::codec::Status::kEncodeFailed;
      }
      cv_.notify_one();
    }
  });

  pc_->onGatheringStateChange([this, whip_endpoint](rtc::PeerConnection::GatheringState state) {
    VC_LOG(video::codec::LogLevel::kDebug,
           std::string("onGatheringStateChange: ") + std::to_string(static_cast<int>(state)) +
           ", offer_ready_=" + std::to_string(offer_ready_));
    if (state == rtc::PeerConnection::GatheringState::Complete) {
      gathering_complete_ = true;
      if (!offer_ready_) {
        auto local_desc = pc_->localDescription();
        if (!local_desc) {
          VC_LOG(video::codec::LogLevel::kError, "  -> no local description available");
          return;
        }
        std::string offer = std::string(*local_desc);
        VC_LOG(video::codec::LogLevel::kDebug,
               std::string("  -> gathering complete, SDP=") + std::to_string(offer.size()) + " bytes");
        auto candidates = ExtractIceCandidates(offer);
        VC_LOG(video::codec::LogLevel::kDebug,
               std::string("  -> ") + std::to_string(candidates.size()) + " ICE candidates in SDP");

        whip_session_->SetOnReady([this](const std::string& sdp) {
          VC_LOG(video::codec::LogLevel::kDebug, "WHIP OnReady (from gathering), setting remote description");
          pc_->setRemoteDescription(sdp);
          session_id_ = whip_session_->SessionId();
          {
            std::lock_guard<std::mutex> lock(mtx_);
            answer_ready_ = true;
          }
          cv_.notify_one();
        });

        whip_session_->SetOnError([this](const std::string& error) {
          VC_LOG(video::codec::LogLevel::kError, std::string("WHIP OnError (from gathering): ") + error);
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
          VC_LOG(video::codec::LogLevel::kError, "WHIP Create (from gathering) failed");
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

  VC_LOG(video::codec::LogLevel::kDebug, "waiting for ICE connected state...");
  {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait_for(lock, std::chrono::seconds(10), [this] { return connected_state_; });
  }

  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("Connect complete, connected=") + std::to_string(connected_));
  return connect_result_;
}

video::codec::Status WebrtcBackend::SendVideo(const video::codec::VideoPacket& packet) {
  if (!video_track_) return video::codec::Status::kNotInitialized;
  if (rtp_config_) {
    uint32_t new_ts;
    if (packet.pts_us > 0) {
      new_ts = rtp_config_->startTimestamp +
               static_cast<uint32_t>((static_cast<uint64_t>(packet.pts_us) * 90) / 1000);
    } else {
      new_ts = rtp_config_->startTimestamp +
               static_cast<uint32_t>(frame_index_ * 3000);
    }
    rtp_config_->timestamp = new_ts;
    frame_index_++;
  }
  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("SendVideo: ") + std::to_string(packet.data.size()) + " bytes, ts=" +
         std::to_string(rtp_config_ ? rtp_config_->timestamp : 0));
  try {
    rtc::message_variant msg(std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(packet.data.data()),
        reinterpret_cast<const std::byte*>(packet.data.data() + packet.data.size())));
    video_track_->send(msg);
    stats_.packets_sent++;
    stats_.bytes_sent += packet.data.size();
    VC_LOG(video::codec::LogLevel::kDebug,
           std::string("SendVideo OK (") + std::to_string(stats_.packets_sent) + " total)");
  } catch (const std::exception& e) {
    VC_LOG(video::codec::LogLevel::kError, std::string("SendVideo FAILED: ") + e.what());
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
  VC_LOG(video::codec::LogLevel::kDebug, "Disconnect() called");
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