#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>

#include <rtc/rtc.hpp>

#include "src/api/stream_backend.h"
#include "src/api/stream_config.h"
#include "src/backend/webrtc/whip_session.h"
#include "src/framework/core/status.h"

namespace video {
namespace stream {

class WebrtcBackend : public StreamBackend {
 public:
  explicit WebrtcBackend(const StreamConfig& config);
  ~WebrtcBackend() override;

  video::codec::Status Connect(const StreamConfig& config) override;
  video::codec::Status SendVideo(const video::codec::VideoPacket& packet) override;
  video::codec::Status SendAudio(const video::codec::AudioPacket& packet) override;
  video::codec::Status Disconnect() override;

  StreamStats GetStats() override;
  void SetStatusCallback(StatusCallback callback) override;

  static std::unique_ptr<StreamBackend> Create(const StreamConfig& config);

 private:
  void OnStateChange(rtc::PeerConnection::State state);

  StreamConfig config_;
  StreamStatus status_;
  std::unique_ptr<WhipSession> whip_session_;
  std::shared_ptr<rtc::PeerConnection> pc_;
  std::shared_ptr<rtc::Track> video_track_;
  // Held so SendVideo() can advance the RTP timestamp per frame. The library
  // never increments it automatically; a constant timestamp makes the receiver
  // treat the whole stream as a single access unit (no decodable frames).
  std::shared_ptr<rtc::RtpPacketizationConfig> rtp_config_;
  StreamStats stats_;
  StatusCallback callback_;
  bool connected_ = false;
  bool gathering_complete_ = false;
  std::string current_offer_;
  std::string session_id_;

  std::mutex mtx_;
  std::condition_variable cv_;
  bool offer_ready_ = false;
  bool answer_ready_ = false;
  bool connected_state_ = false;
  video::codec::Status connect_result_ = video::codec::Status::kOk;
  uint64_t frame_index_ = 0;  // debug: 用于日志输出
};

}  // namespace stream
}  // namespace video