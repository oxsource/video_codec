#pragma once

#include <cstdint>
#include <string>

namespace video {
namespace stream {

struct StreamConfig {
  std::string backend_type;
  std::string remote_url;
  std::string video_codec;
  std::string audio_codec;

  uint32_t initial_bitrate_kbps = 2000;
  uint32_t max_bitrate_kbps = 5000;
  uint32_t min_bitrate_kbps = 200;

  uint32_t resolution_width = 1280;
  uint32_t resolution_height = 720;
  uint32_t framerate = 30;

  uint32_t buffer_duration_s = 30;
  uint32_t reconnect_max_interval_s = 30;

  std::string stun_server;
  std::string turn_server;
};

}  // namespace stream
}  // namespace video