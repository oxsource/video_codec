#pragma once

#include <cstdint>
#include <string>

#include "stream/src/api/network_config.h"

namespace video {
namespace codec {
template <typename T>
class Result;
}  // namespace codec

namespace stream {

// Backend type constants
inline constexpr const char* kBackendWebRTC = "webrtc";

// Codec constants
inline constexpr const char* kCodecH264 = "h264";
inline constexpr const char* kCodecAAC = "aac";
inline constexpr const char* kCodecOpus = "opus";

struct StreamConfig {
  std::string backend_type;
  std::string remote_url;
  std::string video_codec;
  std::string audio_codec;

  // Enable verbose per-frame send logging (SendVideo/SendAudio packet sizes).
  // Runtime replacement for the old compile-time STREAM_DEBUG_SEND macro;
  // default off so release builds carry no per-packet logging cost.
  bool debug = false;

  uint32_t initial_bitrate_kbps = 2000;
  uint32_t max_bitrate_kbps = 5000;
  uint32_t min_bitrate_kbps = 200;

  uint32_t resolution_width = 1280;
  uint32_t resolution_height = 720;
  uint32_t framerate = 30;

  uint32_t buffer_duration_s = 30;
  uint32_t reconnect_max_interval_s = 30;

  // Transport/security settings for signaling (WHIP HTTP requests).
  NetworkConfig network;

  // STUN/TURN (ICE servers).
  std::string stun_server;
  std::string turn_server;

  // ---- Unified JSON configuration ------------------------------------------
  //
  // The stream module owns the JSON schema (field keys and all defaults); the
  // caller only supplies the content. Missing keys fall back to the module's
  // built-in defaults above. The WHIP signaling URL is derived from the signal
  // "host" + "path" as: host + "/" + path + "/whip" (an explicit top-level
  // "url" wins over host/path when present).
  //
  // Implemented in //src/core:stream_core.

  // Parse a JSON configuration string into a StreamConfig. On failure
  // (malformed JSON), logs the reason and returns Status::kInvalidArgument.
  static video::codec::Result<StreamConfig> ParseFromJson(const std::string& json_text);

  // Read a JSON configuration file and parse it. Returns
  // Status::kInvalidArgument if the file cannot be opened or is malformed.
  static video::codec::Result<StreamConfig> LoadFromFile(const std::string& file_path);
};

}  // namespace stream
}  // namespace video