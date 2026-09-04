#include "stream/src/core/reconnect_handler.h"

#include <algorithm>
#include <cstdint>

namespace video {
namespace stream {

ReconnectHandler::ReconnectHandler(uint32_t max_interval_s,
                                   uint32_t buffer_duration_s)
    : max_interval_s_(max_interval_s), buffer_duration_s_(buffer_duration_s) {}

void ReconnectHandler::OnDisconnected(ReconnectCallback callback) {
  reconnecting_ = true;
  current_attempt_ = 0;
  elapsed_since_disconnect_ = 0;
  reconnect_callback_ = std::move(callback);
}

void ReconnectHandler::OnConnected() {
  reconnecting_ = false;
  current_attempt_ = 0;
  elapsed_since_disconnect_ = 0;
}

void ReconnectHandler::Tick() {
  if (!reconnecting_) return;

  elapsed_since_disconnect_++;
  if (elapsed_since_disconnect_ * 1000u >= NextIntervalMs()) {
    // A retry is due: remember this fire, reset the elapsed counter and invoke
    // the reconnect callback. current_attempt_ only advances on a fire so the
    // interval keeps doubling across retries (1s, 2s, 4s, ... up to the cap).
    elapsed_since_disconnect_ = 0;
    current_attempt_++;
    if (reconnect_callback_) reconnect_callback_();
  }
}

uint32_t ReconnectHandler::NextIntervalMs() const {
  const uint32_t max_ms = max_interval_s_ * 1000u;
  // current_attempt_ is the number of fires so far; the gap before the next
  // fire doubles each time (2^attempt seconds: 1s, 2s, 4s, ...).
  const uint32_t shift = std::min<uint32_t>(current_attempt_, 62u);
  uint64_t interval = uint64_t{1000} << shift;
  if (interval > max_ms) interval = max_ms;
  return static_cast<uint32_t>(interval);
}

void ReconnectHandler::Reset() {
  reconnecting_ = false;
  current_attempt_ = 0;
  elapsed_since_disconnect_ = 0;
}

}  // namespace stream
}  // namespace video