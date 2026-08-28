#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace video {
namespace stream {

class ReconnectHandler {
 public:
  using ReconnectCallback = std::function<void()>;

  explicit ReconnectHandler(uint32_t max_interval_s = 30,
                            uint32_t buffer_duration_s = 30);

  void OnDisconnected(ReconnectCallback callback);
  void OnConnected();
  void Tick();
  void Reset();

  bool IsReconnecting() const { return reconnecting_; }
  uint32_t CurrentAttempt() const { return current_attempt_; }
  uint32_t NextIntervalMs() const;

 private:
  uint32_t max_interval_s_;
  uint32_t buffer_duration_s_;
  bool reconnecting_ = false;
  uint32_t current_attempt_ = 0;
  uint32_t elapsed_since_disconnect_ = 0;
  ReconnectCallback reconnect_callback_;
};

}  // namespace stream
}  // namespace video