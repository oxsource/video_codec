// Module log tag: identifies all VC_LOG output from this file (see
// log_slot.h). Must be defined before the first header include so the
// framework's LOG_TAG mechanism picks it up instead of defaulting to __FILE__.
#define LOG_TAG "webrtc_backend"

#include "stream/src/backend/webrtc/webrtc_backend.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <map>
#include <rtc/rtc.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "codec/src/framework/core/log_slot.h"
#include "codec/src/framework/core/status.h"
#include "codec/src/framework/core/types.h"
#include "stream/src/api/stream_config.h"
#include "stream/src/core/video_stream_register.h"

// Per-frame send tracing (SendVideo). Enabled at runtime via
// StreamConfig::debug (the old compile-time STREAM_DEBUG_SEND macro is gone).
#define STREAM_SEND_LOG(level, msg)            \
  do {                                         \
    if (config_.debug) VC_LOG((level), (msg)); \
  } while (0)

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

// Summarises the ICE candidate types (host/srflx/relay/prflx) found in a
// gathered SDP, e.g. "host:2 srflx:1 relay:0 prflx:0" (P0-9).
std::string IceCandidateTypeSummary(const std::string& sdp) {
  std::map<std::string, int> counts;
  std::istringstream stream(sdp);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.find("a=candidate:") != 0) continue;
    std::istringstream tokens(line.substr(12));
    std::string token;
    std::vector<std::string> parts;
    while (tokens >> token) parts.push_back(token);
    // a=candidate:<foundation> <component> <transport> <priority> typ <type>
    // ...
    if (parts.size() > 5) {
      counts[parts[5]]++;
    }
  }
  std::ostringstream out;
  bool first = true;
  for (const auto& kv : counts) {
    if (!first) out << " ";
    first = false;
    out << kv.first << ":" << kv.second;
  }
  return out.str();
}

std::string ToHex(const uint8_t* data, size_t n) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (size_t i = 0; i < n; ++i) {
    out << std::setw(2) << static_cast<unsigned int>(data[i]) << " ";
  }
  return out.str();
}

// Returns true if the first bytes look like an Annex-B start code.
bool IsAnnexBStartCode(const uint8_t* data, size_t n) {
  if (n >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1) {
    return true;
  }
  if (n >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1) {
    return true;
  }
  return false;
}

}  // namespace

WebrtcBackend::WebrtcBackend(const StreamConfig& config) : config_(config) {
  static std::once_flag logger_init;
  std::call_once(logger_init, [] { rtc::InitLogger(rtc::LogLevel::Warning); });
  whip_session_ = std::make_unique<WhipSession>(config_.network);
}

WebrtcBackend::~WebrtcBackend() { Disconnect(); }

void WebrtcBackend::NotifyStreamState(StreamState state) {
  StreamStatus snapshot;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    if (status_.state != state) {
      VC_LOG(video::codec::LogLevel::kDebug,
             std::string("stream state: ") +
                 std::to_string(static_cast<int>(status_.state)) + " -> " +
                 std::to_string(static_cast<int>(state)));
    }
    status_.state = state;
    snapshot = status_;
  }
  // Serialise callback delivery: pc/worker/send threads must never invoke the
  // upper-layer callback concurrently (its state is not internally locked).
  std::lock_guard<std::recursive_mutex> cb_lock(cb_mtx_);
  if (callback_) callback_(snapshot);
}

void WebrtcBackend::OnStateChange(uint64_t gen,
                                  rtc::PeerConnection::State state) {
  if (!IsCurrentSession(gen)) return;
  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("webrtc pc state: ") +
             std::to_string(static_cast<int>(state)) +
             ", connected_state_=" + std::to_string(connected_state_.load()) +
             ", track_open_=" + std::to_string(track_open_.load()));
  switch (state) {
    case rtc::PeerConnection::State::New:
      break;
    case rtc::PeerConnection::State::Connecting:
      NotifyStreamState(StreamState::kConnecting);
      break;
    case rtc::PeerConnection::State::Connected:
      connected_state_.store(true);
      cv_.notify_all();
      NotifyStreamState(StreamState::kIceConnected);
      break;
    case rtc::PeerConnection::State::Disconnected:
      connected_state_.store(false);
      track_open_.store(false);
      cv_.notify_all();
      NotifyStreamState(StreamState::kDisconnected);
      break;
    case rtc::PeerConnection::State::Failed:
    case rtc::PeerConnection::State::Closed:
      connected_state_.store(false);
      track_open_.store(false);
      pc_terminal_.store(true);
      cv_.notify_all();
      NotifyStreamState(state == rtc::PeerConnection::State::Closed
                            ? StreamState::kDestroyed
                            : StreamState::kDisconnected);
      break;
  }
}

void WebrtcBackend::OnLocalDescription(uint64_t gen, const std::string& offer) {
  if (!IsCurrentSession(gen)) return;
  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("onLocalDescription: ") + std::to_string(offer.size()) +
             " bytes, offer_ready_=" + std::to_string(offer_ready_.load()) +
             ", gathering_complete_=" +
             std::to_string(gathering_complete_.load()));

  // The offer we care about is the final one that carries all gathered ICE
  // candidates. When gathering is still running, the onGatheringStateChange
  // handler reads pc_->localDescription() and sends from there.
  if (gathering_complete_.load() && ClaimOfferSend()) {
    SendOfferToWhip(gen, offer);
  }
}

void WebrtcBackend::OnGatheringStateChange(
    uint64_t gen, rtc::PeerConnection::GatheringState state) {
  if (!IsCurrentSession(gen)) return;
  bool was_complete = gathering_complete_.load();
  if (state == rtc::PeerConnection::GatheringState::Complete) {
    gathering_complete_.store(true);
    if (!was_complete) {
      VC_LOG(video::codec::LogLevel::kDebug,
             "gathering_complete_: false -> true");
    }

    if (!ClaimOfferSend()) {
      VC_LOG(video::codec::LogLevel::kDebug,
             "  -> offer already sent, ignoring gathering-complete");
      return;
    }

    auto pc = CurrentPeerConnection();
    if (!pc) return;
    auto local_desc = pc->localDescription();
    if (!local_desc) {
      VC_LOG(video::codec::LogLevel::kError,
             "  -> no local description available at gathering complete");
      return;
    }
    std::string offer = std::string(*local_desc);
    auto candidates = ExtractIceCandidates(offer);
    VC_LOG(video::codec::LogLevel::kDebug,
           std::string("  -> gathering complete, SDP=") +
               std::to_string(offer.size()) + " bytes, " +
               std::to_string(candidates.size()) + " candidates (" +
               IceCandidateTypeSummary(offer) + ")");
    SendOfferToWhip(gen, offer);
  } else {
    gathering_complete_.store(false);
    if (was_complete) {
      VC_LOG(video::codec::LogLevel::kDebug,
             "gathering_complete_: true -> false");
    }
  }
}

void WebrtcBackend::OnTrackOpen(uint64_t gen) {
  if (!IsCurrentSession(gen)) return;
  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("track_open_: false -> true"));
  track_open_.store(true);
  TryMarkStreaming();
}

void WebrtcBackend::OnTrackClosed(uint64_t gen) {
  if (!IsCurrentSession(gen)) return;
  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("track_open_: true -> false"));
  track_open_.store(false);
}

void WebrtcBackend::OnWhipReady(uint64_t gen, const std::string& sdp) {
  if (!IsCurrentSession(gen)) return;
  VC_LOG(video::codec::LogLevel::kDebug, std::string("WHIP answer received, ") +
                                             std::to_string(sdp.size()) +
                                             " bytes");
  auto pc = CurrentPeerConnection();
  if (!pc) {
    VC_LOG(video::codec::LogLevel::kWarn,
           "WHIP answer dropped: session already torn down");
    return;
  }
  try {
    pc->setRemoteDescription(sdp);
  } catch (const std::exception& e) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("setRemoteDescription failed: ") + e.what());
    FailConnect(video::codec::Status::kNetworkError,
                std::string("setRemoteDescription failed: ") + e.what());
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mtx_);
    answer_ready_.store(true);
  }
  cv_.notify_all();
}

void WebrtcBackend::OnWhipError(uint64_t gen, const std::string& error) {
  if (!IsCurrentSession(gen)) return;
  VC_LOG(video::codec::LogLevel::kError, std::string("WHIP error: ") + error);
  FailConnect(video::codec::Status::kNetworkError, error);
}

bool WebrtcBackend::ClaimOfferSend() {
  bool expected = false;
  bool claimed = offer_ready_.compare_exchange_strong(expected, true);
  if (claimed) {
    VC_LOG(video::codec::LogLevel::kDebug,
           std::string("offer_ready_: false -> true (claimed)"));
  }
  return claimed;
}

void WebrtcBackend::SendOfferToWhip(uint64_t gen, const std::string& offer) {
  VC_LOG(video::codec::LogLevel::kDebug, std::string("SendOfferToWhip: ") +
                                             std::to_string(offer.size()) +
                                             " bytes to " + whip_endpoint_);
  std::thread worker;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!IsCurrentSession(gen) || whip_thread_.joinable()) return;
    std::string endpoint = whip_endpoint_;
    worker = std::thread([this, gen, endpoint, offer] {
      WhipCreateWorker(gen, std::move(endpoint), offer);
    });
    whip_thread_ = std::move(worker);
  }
}

void WebrtcBackend::WhipCreateWorker(uint64_t gen, std::string endpoint,
                                     const std::string& offer) {
  if (!IsCurrentSession(gen)) return;
  whip_session_->SetOnReady(
      [this, gen](const std::string& sdp) { OnWhipReady(gen, sdp); });
  whip_session_->SetOnError(
      [this, gen](const std::string& error) { OnWhipError(gen, error); });

  if (!whip_session_->Create(endpoint, offer)) {
    // Create() reports through on_error_ on every failure path; the defensive
    // signal below only covers the "answer not yet signalled" edge case.
    if (IsCurrentSession(gen)) {
      FailConnect(video::codec::Status::kNetworkError, "WHIP Create failed");
    }
  }
}

void WebrtcBackend::JoinWhipWorker() {
  std::thread worker;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    worker = std::move(whip_thread_);
  }
  if (worker.joinable()) {
    worker.join();
  }
}

void WebrtcBackend::TeardownConnection() {
  // Invalidate every callback registered under the current session so events
  // delivered while closing (e.g. Closed) cannot touch a newer session or
  // override the terminal state reported by the caller.
  session_gen_.fetch_add(1);

  std::shared_ptr<rtc::PeerConnection> old_pc;
  std::shared_ptr<rtc::Track> old_track;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    old_pc = std::move(pc_);
    old_track = std::move(video_track_);
    rtp_config_.reset();
    rtcp_sr_reporter_.reset();
  }
  if (old_pc) {
    try {
      old_pc->close();
    } catch (const std::exception& e) {
      VC_LOG(video::codec::LogLevel::kWarn,
             std::string("PeerConnection close raised: ") + e.what());
    }
  }
  old_track.reset();
  JoinWhipWorker();
}

std::shared_ptr<rtc::PeerConnection> WebrtcBackend::CurrentPeerConnection() {
  std::lock_guard<std::mutex> lock(mtx_);
  return pc_;
}

void WebrtcBackend::FailConnect(video::codec::Status status,
                                const std::string& error) {
  {
    std::lock_guard<std::mutex> lock(mtx_);
    status_.last_error = error;
    if (!answer_ready_.load()) {
      connect_result_ = status;
      answer_ready_.store(true);
    }
  }
  cv_.notify_all();
}

void WebrtcBackend::ResetConnectState() {
  offer_ready_.store(false);
  gathering_complete_.store(false);
  answer_ready_.store(false);
  connected_state_.store(false);
  track_open_.store(false);
  pc_terminal_.store(false);
  first_frame_sent_.store(false);
  streaming_reported_.store(false);
  {
    std::lock_guard<std::mutex> lock(mtx_);
    connect_result_ = video::codec::Status::kOk;
  }
  frame_index_ = 0;
  annexb_checked_ = false;
  auto now = std::chrono::steady_clock::now();
  last_sr_time_ = now;
  last_drop_log_time_ = now;
}

void WebrtcBackend::TryMarkStreaming() {
  if (!track_open_.load() || !first_frame_sent_.load()) return;
  if (streaming_reported_.exchange(true)) return;
  NotifyStreamState(StreamState::kStreaming);
}

video::codec::Status WebrtcBackend::Connect(const StreamConfig& config) {
  config_ = config;
  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("Connect: url=") + config_.remote_url);

  if (config_.remote_url.empty()) {
    return video::codec::Status::kInvalidArgument;
  }

  whip_endpoint_ = config_.remote_url;
  if (!whip_endpoint_.empty() && whip_endpoint_.back() == '/') {
    whip_endpoint_.pop_back();
  }

  // Tear down any previous session (reconnect path) before starting fresh so a
  // stale PeerConnection / WHIP worker can never interfere with this one.
  whip_session_->Reset();
  TeardownConnection();
  ResetConnectState();
  uint64_t gen = session_gen_.load();

  rtc::Configuration pc_config;
  pc_config.iceServers.emplace_back("stun:stun.l.google.com:19302");
  VC_LOG(video::codec::LogLevel::kDebug, "creating PeerConnection");

  {
    std::lock_guard<std::mutex> lock(mtx_);
    pc_ = std::make_shared<rtc::PeerConnection>(pc_config);
  }

  pc_->onStateChange([this, gen](rtc::PeerConnection::State state) {
    OnStateChange(gen, state);
  });

  pc_->onLocalDescription([this, gen](const rtc::Description& description) {
    OnLocalDescription(gen, std::string(description));
  });

  pc_->onGatheringStateChange(
      [this, gen](rtc::PeerConnection::GatheringState state) {
        OnGatheringStateChange(gen, state);
      });

  const rtc::SSRC ssrc = 42;
  rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
  media.addH264Codec(96);
  media.addSSRC(ssrc, "video-send");
  video_track_ = pc_->addTrack(media);

  rtp_config_ = std::make_shared<rtc::RtpPacketizationConfig>(
      ssrc, "video-send", 96, 90000);
  auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
      rtc::NalUnit::Separator::StartSequence, rtp_config_);
  rtcp_sr_reporter_ = std::make_shared<rtc::RtcpSrReporter>(rtp_config_);
  packetizer->addToChain(rtcp_sr_reporter_);
  packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
  video_track_->setMediaHandler(packetizer);

  video_track_->onOpen([this, gen]() { OnTrackOpen(gen); });
  video_track_->onClosed([this, gen]() { OnTrackClosed(gen); });

  pc_->setLocalDescription();

  VC_LOG(video::codec::LogLevel::kDebug, "waiting for WHIP answer...");
  {
    std::unique_lock<std::mutex> lock(mtx_);
    bool signalled = cv_.wait_for(lock, std::chrono::seconds(10), [this] {
      return answer_ready_.load() || pc_terminal_.load();
    });
    if (!signalled) {
      lock.unlock();
      VC_LOG(video::codec::LogLevel::kError, "timeout waiting for WHIP answer");
      TeardownConnection();
      return video::codec::Status::kTimeout;
    }
    auto st = connect_result_;
    bool answered = answer_ready_.load();
    lock.unlock();
    if (st != video::codec::Status::kOk) {
      TeardownConnection();
      return st;
    }
    if (!answered) {
      VC_LOG(video::codec::LogLevel::kError,
             "PeerConnection reached terminal state before WHIP answer");
      TeardownConnection();
      return video::codec::Status::kNetworkError;
    }
  }

  VC_LOG(video::codec::LogLevel::kDebug, "waiting for ICE connected state...");
  {
    std::unique_lock<std::mutex> lock(mtx_);
    bool signalled = cv_.wait_for(lock, std::chrono::seconds(10), [this] {
      return connected_state_.load() || pc_terminal_.load();
    });
    if (!signalled) {
      lock.unlock();
      VC_LOG(video::codec::LogLevel::kError,
             "timeout waiting for ICE connected state");
      TeardownConnection();
      return video::codec::Status::kTimeout;
    }
    bool connected = connected_state_.load();
    lock.unlock();
    if (!connected) {
      VC_LOG(video::codec::LogLevel::kError,
             "PeerConnection reached terminal state before ICE connected");
      TeardownConnection();
      return video::codec::Status::kNetworkError;
    }
  }

  VC_LOG(video::codec::LogLevel::kDebug,
         std::string("Connect complete, connected=") +
             std::to_string(connected_state_.load()) +
             ", track_open=" + std::to_string(track_open_.load()));
  return video::codec::Status::kOk;
}

video::codec::Status WebrtcBackend::SendVideo(
    const video::codec::VideoPacket& packet) {
  // Snapshot the media objects under the lock so a concurrent Disconnect()
  // (which moves them out under mtx_) can never race this frame.
  std::shared_ptr<rtc::Track> track;
  std::shared_ptr<rtc::RtpPacketizationConfig> rtp_config;
  std::shared_ptr<rtc::RtcpSrReporter> sr;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    track = video_track_;
    rtp_config = rtp_config_;
    sr = rtcp_sr_reporter_;
  }
  if (!track) return video::codec::Status::kNotInitialized;
  if (!connected_state_.load() && !track_open_.load()) {
    VC_LOG(video::codec::LogLevel::kDebug,
           "SendVideo skipped: media path not open yet");
    return video::codec::Status::kNotInitialized;
  }

  // One-shot Annex-B vs AVCC sniff on the first frame (P0-6, diagnostic only).
  if (!annexb_checked_) {
    {
      std::lock_guard<std::mutex> lock(mtx_);
      if (!annexb_checked_) {
        annexb_checked_ = true;
        const auto* data = reinterpret_cast<const uint8_t*>(packet.data.data());
        size_t n = std::min(packet.data.size(), size_t{16});
        bool annexb = n > 0 && IsAnnexBStartCode(data, n);
        VC_LOG(annexb ? video::codec::LogLevel::kDebug
                      : video::codec::LogLevel::kWarn,
               std::string("first packet.data bytes (") +
                   (annexb ? "Annex-B start code" : "not Annex-B") +
                   "): " + ToHex(data, n));
      }
    }
  }

  if (rtp_config) {
    uint32_t new_ts;
    if (packet.pts_us > 0) {
      new_ts = rtp_config->startTimestamp +
               static_cast<uint32_t>(
                   (static_cast<uint64_t>(packet.pts_us) * 90) / 1000);
    } else {
      new_ts = rtp_config->startTimestamp +
               static_cast<uint32_t>(frame_index_ * 3000);
    }
    rtp_config->timestamp = new_ts;
    frame_index_++;
  }

  auto now = std::chrono::steady_clock::now();
  // RtcpSrReporter only emits a Sender Report once setNeedsToReport() has been
  // called (no internal timer); drive it at ~1 Hz (P1-3).
  if (sr && now - last_sr_time_ >= std::chrono::seconds(1)) {
    last_sr_time_ = now;
    sr->setNeedsToReport();
  }

  STREAM_SEND_LOG(video::codec::LogLevel::kDebug,
                  std::string("SendVideo: ") +
                      std::to_string(packet.data.size()) + " bytes, ts=" +
                      std::to_string(rtp_config ? rtp_config->timestamp : 0));
  bool sent = false;
  try {
    rtc::message_variant msg(std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(packet.data.data()),
        reinterpret_cast<const std::byte*>(packet.data.data() +
                                           packet.data.size())));
    sent = track->send(msg);
  } catch (const std::exception& e) {
    VC_LOG(video::codec::LogLevel::kError,
           std::string("SendVideo FAILED: ") + e.what());
    {
      std::lock_guard<std::mutex> lock(mtx_);
      status_.last_error = e.what();
    }
    return video::codec::Status::kEncodeFailed;
  }

  if (!sent) {
    // send() returns false under backpressure: count it, do not fake success.
    // Non-fatal by design (M4): the frame stays counted as dropped in
    // stats_.packets_dropped rather than bumping the upper layer's fatal
    // frames_dropped/backpressure accounting.
    stats_.packets_dropped++;
    if (now - last_drop_log_time_ >= std::chrono::seconds(1)) {
      last_drop_log_time_ = now;
      VC_LOG(video::codec::LogLevel::kWarn,
             std::string("SendVideo backpressure drop (cumulative dropped=") +
                 std::to_string(stats_.packets_dropped) + ")");
    }
    return video::codec::Status::kOk;
  }

  stats_.packets_sent++;
  stats_.bytes_sent += packet.data.size();
  first_frame_sent_.store(true);
  TryMarkStreaming();
  STREAM_SEND_LOG(video::codec::LogLevel::kDebug,
                  std::string("SendVideo OK (") +
                      std::to_string(stats_.packets_sent) + " total)");
  return video::codec::Status::kOk;
}

video::codec::Status WebrtcBackend::SendAudio(
    const video::codec::AudioPacket& packet) {
  (void)packet;
  // No audio track is implemented in this backend. Surface that honestly
  // instead of inflating packets_sent/bytes_sent (P1-6).
  if (!audio_unsupported_logged_.exchange(true)) {
    VC_LOG(video::codec::LogLevel::kWarn,
           "SendAudio: audio is not supported by the webrtc backend");
    std::lock_guard<std::mutex> lock(mtx_);
    status_.last_error = "audio not supported by webrtc backend";
  }
  return video::codec::Status::kUnsupportedOperation;
}

video::codec::Status WebrtcBackend::Disconnect() {
  VC_LOG(video::codec::LogLevel::kDebug, "Disconnect() called");
  // Invalidate callbacks first so pc_->close() below cannot report a stale
  // kDestroyed over our kDisconnected, or touch a future session.
  session_gen_.fetch_add(1);
  if (whip_session_) {
    bool deleted = whip_session_->Delete();
    VC_LOG(deleted ? video::codec::LogLevel::kDebug
                   : video::codec::LogLevel::kWarn,
           std::string("WHIP session DELETE ") +
               (deleted ? "ok" : "failed/skipped"));
  }
  TeardownConnection();
  connected_state_.store(false);
  track_open_.store(false);
  NotifyStreamState(StreamState::kDisconnected);
  return video::codec::Status::kOk;
}

StreamStats WebrtcBackend::GetStats() { return stats_; }

void WebrtcBackend::SetStatusCallback(StatusCallback callback) {
  callback_ = std::move(callback);
}

std::unique_ptr<StreamBackend> WebrtcBackend::Create(
    const StreamConfig& config) {
  return std::make_unique<WebrtcBackend>(config);
}

VIDEO_STREAM_REGISTER(video::stream::kBackendWebRTC, WebrtcBackend::Create);

}  // namespace stream
}  // namespace video
