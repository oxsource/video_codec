#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "codec/src/framework/core/status.h"
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

  video::codec::Status SendVideo(
      const video::codec::VideoPacket& packet) override;
  video::codec::Status SendAudio(
      const video::codec::AudioPacket& packet) override;

  video::codec::Status UpdateConfig(const StreamConfig& config) override;

  StreamStatus GetStatus() const override;
  void SetStatusCallback(StatusCallback callback) override;

 private:
  void UpdateStatus(StreamState state);
  video::codec::Status ValidateConfig(const StreamConfig& config) const;
  void OnBackendStatusChange(const StreamStatus& s);

  // Internal auto-reconnect machinery: a single persistent worker thread ticks
  // the ReconnectHandler (~1 Hz) while a reconnect is armed, so both runtime
  // drops and an initial Connect() failure are retried without any external
  // driver. Must be started (Init) before any reconnect can be scheduled and
  // always joined (Stop/Release) before the handler/backend are torn down.
  void StartReconnectWorker();
  void StopReconnectWorker();
  void ScheduleReconnect();
  void AttemptReconnect();
  void RunReconnectWorker();

  static constexpr uint32_t kBackpressureMaxPackets = 100;

  StreamConfig config_;
  StreamStatus status_;
  std::unique_ptr<StreamBackend> backend_;
  std::unique_ptr<AbrController> abr_controller_;
  std::unique_ptr<ReconnectHandler> reconnect_handler_;
  StatusCallback callback_;
  uint32_t backpressure_buffer_count_ = 0;

  // Guards status_/callback_/counters, which are touched by the user-facing
  // thread (Start/Stop/SendVideo), the backend threads (status callbacks) and
  // the reconnect worker thread (reconnect attempts).
  mutable std::mutex status_mtx_;
  std::mutex re_mtx_;
  std::condition_variable re_cv_;
  std::thread reconnect_thread_;
  std::atomic<bool> re_stop_{false};
  std::atomic<bool> reconnect_armed_{false};
};

}  // namespace stream
}  // namespace video