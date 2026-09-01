#include "stream/src/core/reconnect_handler.h"

#include <algorithm>
#include <cmath>

namespace video {
namespace stream {

ReconnectHandler::ReconnectHandler(uint32_t max_interval_s, uint32_t buffer_duration_s)
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
  current_attempt_++;

  uint32_t interval_ms = NextIntervalMs();

  if (elapsed_since_disconnect_ * 1000 >= interval_ms) {
    elapsed_since_disconnect_ = 0;
    if (reconnect_callback_) reconnect_callback_();
  }
}

uint32_t ReconnectHandler::NextIntervalMs() const {
  uint32_t interval = 1000 * static_cast<uint32_t>(std::pow(2, current_attempt_ - 1));
  return std::min(interval, max_interval_s_ * 1000);
}

void ReconnectHandler::Reset() {
  reconnecting_ = false;
  current_attempt_ = 0;
  elapsed_since_disconnect_ = 0;
}

}  // namespace stream
}  // namespace video