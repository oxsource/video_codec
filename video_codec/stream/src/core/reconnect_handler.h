#pragma once

#include <cstdint>
#include <functional>

namespace video {
namespace stream {

// Drives exponential-backoff reconnect scheduling. State changes should only
// be performed by the single thread that owns reconnection (the internal
// reconnect worker); Reset() may be called by a user thread after that worker
// has been joined.
class ReconnectHandler {
 public:
  using ReconnectCallback = std::function<void()>;

  explicit ReconnectHandler(uint32_t max_interval_s = 30,
                            uint32_t buffer_duration_s = 30);

  // Begins a reconnect session: resets the attempt counter and stores the
  // callback that Tick() fires when a retry is due.
  void OnDisconnected(ReconnectCallback callback);
  // Stops the reconnect session (called once a connection is established).
  void OnConnected();
  // Advances the scheduler by one tick (call ~once per second). Fires the
  // callback once the backoff interval for the current attempt has elapsed.
  void Tick();
  void Reset();

  bool IsReconnecting() const { return reconnecting_; }
  uint32_t CurrentAttempt() const { return current_attempt_; }
  uint32_t NextIntervalMs() const;

 private:
  uint32_t max_interval_s_;
  // Reserved: buffer-duration-aware reconnect logic is not yet implemented.
  [[maybe_unused]] uint32_t buffer_duration_s_;
  bool reconnecting_ = false;
  // Number of times the reconnect callback has been fired so far.
  uint32_t current_attempt_ = 0;
  // Whole seconds elapsed since the last fire (or since OnDisconnected).
  uint32_t elapsed_since_disconnect_ = 0;
  ReconnectCallback reconnect_callback_;
};

}  // namespace stream
}  // namespace video