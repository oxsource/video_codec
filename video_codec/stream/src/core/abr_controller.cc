#include "src/core/abr_controller.h"

#include <algorithm>
#include <cmath>

namespace video {
namespace stream {

AbrController::AbrController(uint32_t initial_bitrate_kbps,
                             uint32_t min_bitrate_kbps,
                             uint32_t max_bitrate_kbps)
    : current_bitrate_kbps_(initial_bitrate_kbps),
      min_bitrate_kbps_(min_bitrate_kbps),
      max_bitrate_kbps_(max_bitrate_kbps) {
  current_bitrate_kbps_ = std::clamp(current_bitrate_kbps_, min_bitrate_kbps_, max_bitrate_kbps_);
}

void AbrController::Update(uint32_t rtt_ms, float packet_loss_pct) {
  if (packet_loss_pct > 5.0f) {
    uint32_t reduction = std::max<uint32_t>(current_bitrate_kbps_ / 2, 100);
    current_bitrate_kbps_ = std::max(current_bitrate_kbps_ - reduction, min_bitrate_kbps_);
    consecutive_good_intervals_ = 0;
  } else if (packet_loss_pct > 2.0f) {
    uint32_t reduction = std::max<uint32_t>(current_bitrate_kbps_ / 4, 50);
    current_bitrate_kbps_ = std::max(current_bitrate_kbps_ - reduction, min_bitrate_kbps_);
    consecutive_good_intervals_ = 0;
  } else {
    consecutive_good_intervals_++;
    if (consecutive_good_intervals_ >= 5) {
      uint32_t increase = std::max<uint32_t>(current_bitrate_kbps_ / 8, 50);
      current_bitrate_kbps_ = std::min(current_bitrate_kbps_ + increase, max_bitrate_kbps_);
      consecutive_good_intervals_ = 0;
    }
  }
}

void AbrController::Reset() {
  current_bitrate_kbps_ = max_bitrate_kbps_;
  consecutive_good_intervals_ = 0;
}

}  // namespace stream
}  // namespace video