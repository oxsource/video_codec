#pragma once

#include <memory>

#include "stream/src/api/stream.h"
#include "stream/src/api/stream_backend.h"
#include "stream/src/api/stream_config.h"
#include "stream/src/api/stream_status.h"
#include "stream/src/core/abr_controller.h"
#include "stream/src/core/reconnect_handler.h"

namespace video {
namespace stream {

class StreamImpl : public Stream {
 public:
  explicit StreamImpl(const StreamConfig& config);
  ~StreamImpl() override;

  video::codec::Status Init() override;
  video::codec::Status Start() override;
  video::codec::Status Stop() override;
  void Release() override;

  video::codec::Status SendVideo(const video::codec::VideoPacket& packet) override;
  video::codec::Status SendAudio(const video::codec::AudioPacket& packet) override;

  video::codec::Status UpdateConfig(const StreamConfig& config) override;

  StreamStatus GetStatus() const override;
  void SetStatusCallback(StatusCallback callback) override;

 private:
  void UpdateStatus(StreamState state);
  video::codec::Status ValidateConfig(const StreamConfig& config) const;
  void OnBackendStatusChange(const StreamStatus& s);
  void OnReconnectTick();

  static constexpr uint32_t kBackpressureMaxPackets = 100;

  StreamConfig config_;
  StreamStatus status_;
  std::unique_ptr<StreamBackend> backend_;
  std::unique_ptr<AbrController> abr_controller_;
  std::unique_ptr<ReconnectHandler> reconnect_handler_;
  StatusCallback callback_;
  uint32_t backpressure_buffer_count_ = 0;
};

}  // namespace stream
}  // namespace video