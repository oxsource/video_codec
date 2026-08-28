#pragma once

#include <cstdint>

namespace video {
namespace stream {

struct StreamStats;

class AbrController {
 public:
  AbrController(uint32_t initial_bitrate_kbps,
                uint32_t min_bitrate_kbps,
                uint32_t max_bitrate_kbps);

  uint32_t CurrentBitrateKbps() const { return current_bitrate_kbps_; }
  uint32_t MinBitrateKbps() const { return min_bitrate_kbps_; }
  uint32_t MaxBitrateKbps() const { return max_bitrate_kbps_; }

  void Update(uint32_t rtt_ms, float packet_loss_pct);
  void Reset();

 private:
  uint32_t current_bitrate_kbps_;
  uint32_t min_bitrate_kbps_;
  uint32_t max_bitrate_kbps_;
  uint32_t consecutive_good_intervals_ = 0;
};

}  // namespace stream
}  // namespace video