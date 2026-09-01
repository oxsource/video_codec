#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "stream/src/api/stream_config.h"
#include "stream/src/api/stream_status.h"

namespace video {
namespace codec {
enum class Status;
struct VideoPacket;
struct AudioPacket;
}  // namespace codec
}  // namespace video

namespace video {
namespace stream {

struct StreamStats {
  uint32_t rtt_ms = 0;
  float packet_loss_pct = 0.0f;
  uint32_t jitter_ms = 0;
  uint64_t bytes_sent = 0;
  uint64_t packets_sent = 0;
};

class StreamBackend {
 public:
  StreamBackend() = default;
  virtual ~StreamBackend() = default;

  virtual video::codec::Status Connect(const StreamConfig& config) = 0;
  virtual video::codec::Status SendVideo(const video::codec::VideoPacket& packet) = 0;
  virtual video::codec::Status SendAudio(const video::codec::AudioPacket& packet) = 0;
  virtual video::codec::Status Disconnect() = 0;

  virtual StreamStats GetStats() = 0;
  virtual void SetStatusCallback(StatusCallback callback) = 0;

  StreamBackend(const StreamBackend&) = delete;
  StreamBackend& operator=(const StreamBackend&) = delete;
};

using BackendFactory = std::unique_ptr<StreamBackend> (*)(const StreamConfig&);

void RegisterBackend(const std::string& name, BackendFactory factory);

}  // namespace stream
}  // namespace video