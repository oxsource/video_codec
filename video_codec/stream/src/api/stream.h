#pragma once

#include <memory>

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

class Stream {
 public:
  static std::unique_ptr<Stream> Create(const StreamConfig& config);

  Stream() = default;
  virtual ~Stream() = default;

  virtual video::codec::Status Init() = 0;
  virtual video::codec::Status Start() = 0;
  virtual video::codec::Status Stop() = 0;
  virtual void Release() = 0;

  virtual video::codec::Status SendVideo(const video::codec::VideoPacket& packet) = 0;
  virtual video::codec::Status SendAudio(const video::codec::AudioPacket& packet) = 0;

  virtual video::codec::Status UpdateConfig(const StreamConfig& config) = 0;

  virtual StreamStatus GetStatus() const = 0;
  virtual void SetStatusCallback(StatusCallback callback) = 0;

  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;
};

}  // namespace stream
}  // namespace video