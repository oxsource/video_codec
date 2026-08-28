#include "src/backend/mock/mock_backend.h"

#include "src/framework/core/status.h"
#include "src/framework/core/types.h"

namespace video {
namespace stream {

MockBackend::MockBackend(const StreamConfig& config) : config_(config) {}

video::codec::Status MockBackend::Connect(const StreamConfig& config) {
  config_ = config;
  connected_ = true;
  StreamStatus status;
  status.state = StreamState::kStreaming;
  if (callback_) callback_(status);
  return video::codec::Status::kOk;
}

video::codec::Status MockBackend::SendVideo(const video::codec::VideoPacket& packet) {
  if (!connected_) return video::codec::Status::kNotInitialized;
  video_packets_sent_++;
  return video::codec::Status::kOk;
}

video::codec::Status MockBackend::SendAudio(const video::codec::AudioPacket& packet) {
  if (!connected_) return video::codec::Status::kNotInitialized;
  audio_packets_sent_++;
  return video::codec::Status::kOk;
}

video::codec::Status MockBackend::Disconnect() {
  connected_ = false;
  StreamStatus status;
  status.state = StreamState::kDisconnected;
  if (callback_) callback_(status);
  return video::codec::Status::kOk;
}

StreamStats MockBackend::GetStats() {
  StreamStats stats;
  stats.bytes_sent = (video_packets_sent_ + audio_packets_sent_) * 1024;
  stats.packets_sent = video_packets_sent_ + audio_packets_sent_;
  return stats;
}

void MockBackend::SetStatusCallback(StatusCallback callback) {
  callback_ = std::move(callback);
}

}  // namespace stream
}  // namespace video