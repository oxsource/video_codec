#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace video {
namespace stream {

enum class StreamState {
  kCreated,
  kConfigured,
  kConnecting,
  kStreaming,
  kReconnecting,
  kDisconnected,
  kDestroyed,
  // WebRTC-only refinement: ICE/DTLS-SRTP is up (PC Connected) but the media
  // track is not open yet, so no frame has been confirmed sent. kStreaming is
  // only reported once the track is open AND a frame was sent successfully.
  kIceConnected
};

struct StreamStatus {
  StreamState state = StreamState::kCreated;
  uint32_t bitrate_kbps = 0;
  uint32_t rtt_ms = 0;
  float packet_loss_pct = 0.0f;
  uint64_t bytes_sent = 0;
  uint64_t frames_sent = 0;
  uint64_t frames_dropped = 0;
  uint64_t uptime_s = 0;
  std::string last_error;
};

using StatusCallback = std::function<void(const StreamStatus&)>;

}  // namespace stream
}  // namespace video