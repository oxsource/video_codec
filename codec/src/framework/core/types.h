// types.h
#pragma once

#include <cstdint>
#include <vector>

namespace video {
namespace codec {

// ---- Codec / format enums -------------------------------------------------

enum class VideoCodecType { kH264, kHEVC };
enum class AudioCodecType { kNone, kAAC, kOpus };  // kNone = no audio stream
enum class PixelFormat { kI420, kNV12, kRGBA };
enum class SampleFormat { kS16, kF32, kS16Planar, kF32Planar };
enum class BitrateMode { kConstant, kVariable };

// Which backend understands a NativeBuffer / which backend to force.
enum class Backend { kAuto, kAndroid, kFFmpeg };

// Canonical string name of a backend, e.g. BackendToString(Backend::kFFmpeg) ==
// "ffmpeg", BackendToString(Backend::kAndroid) == "android". Returns "unknown"
// for an out-of-range value. A free function (not a member): C++ enum classes
// cannot declare member functions. Mirrors StatusToString (status.h) so log
// lines and error paths can name a backend; these lowercase names also back
// the example's --backend values and output-file suffixes.
inline const char* BackendToString(Backend b) {
  switch (b) {
    case Backend::kAuto:
      return "auto";
    case Backend::kAndroid:
      return "android";
    case Backend::kFFmpeg:
      return "ffmpeg";
  }
  return "unknown";
}

// ---- Frame / packet value types ------------------------------------------

struct VideoFrame {
  PixelFormat format = PixelFormat::kNV12;
  int width = 0;
  int height = 0;
  int64_t timestamp_us = 0;
  std::vector<uint8_t> planes[3];  // I420: Y,U,V; NV12: Y,UV; RGBA: planes[0]
  int stride[3] = {0, 0, 0};
};

struct AudioFrame {
  SampleFormat format = SampleFormat::kS16;
  int sample_rate = 48000;
  int channels = 2;
  int64_t timestamp_us = 0;
  std::vector<uint8_t> data;  // interleaved PCM
};

// Encoded video output. `data` is Annex-B preferred; `keyframe` marks IDR.
struct VideoPacket {
  std::vector<uint8_t> data;
  int64_t pts_us = 0;
  bool keyframe = false;
};

// Encoded audio output (e.g. ADTS AAC). `keyframe` is always false for audio.
struct AudioPacket {
  std::vector<uint8_t> data;
  int64_t pts_us = 0;
  bool keyframe = false;
};

// Zero-copy pointer object. The framework NEVER takes ownership of `handle`;
// the caller keeps the underlying buffer alive for the Encode() call duration.
struct NativeBuffer {
  Backend backend = Backend::kAuto;
  void* handle = nullptr;  // AHardwareBuffer* (Android) / device ptr (FFmpeg HW)
  PixelFormat format = PixelFormat::kNV12;
  int width = 0;
  int height = 0;
  int64_t timestamp_us = 0;
  int fence_fd = -1;
};

// ---- Encoder configuration ------------------------------------------------

struct VideoConfig {
  VideoCodecType codec = VideoCodecType::kH264;
  int width = 0;
  int height = 0;
  int fps = 30;
  int bitrate = 4'000'000;
  BitrateMode bitrate_mode = BitrateMode::kConstant;
  int gop_size = 0;  // 0 = auto
  PixelFormat input_format = PixelFormat::kNV12;
  Backend backend = Backend::kAuto;  // kAuto -> platform select

  // True when the config describes an encodable stream (dimensions set).
  // Backends call this to reject bad configs before doing any real work.
  bool IsValid() const { return width > 0 && height > 0; }
};

struct AudioConfig {
  AudioCodecType codec = AudioCodecType::kAAC;
  int sample_rate = 48000;
  int channels = 2;
  int bitrate = 128'000;
  Backend backend = Backend::kAuto;

  // True when the config describes an encodable stream (rate + channels set).
  bool IsValid() const { return sample_rate > 0 && channels > 0; }
};

// ---- Muxer configuration ------------------------------------------------

enum class MuxFormat { kMp4 };  // v1; future: kMkv, kTs, kWebm

struct MuxerConfig {
  MuxFormat format = MuxFormat::kMp4;
  bool fragmented = true;  // fMP4: header upfront + per-keyframe fragments
  int width = 0;           // stream metadata; SPS-parse fallbacks
  int height = 0;
  int fps = 30;
  // Optional audio stream metadata. kNone (default) produces a video-only
  // container; kAAC adds an AAC-LC track (v1: the only muxed audio codec).
  AudioCodecType audio_codec = AudioCodecType::kNone;
  int sample_rate = 48000;
  int channels = 2;
  Backend backend = Backend::kAuto;  // kAuto -> platform select

  // True when the config describes a muxable stream (dimensions set, and the
  // optional audio metadata is valid when an audio track is requested).
  bool IsValid() const {
    if (width <= 0 || height <= 0) return false;
    if (audio_codec != AudioCodecType::kNone) {
      return sample_rate > 0 && channels > 0;
    }
    return true;
  }
};

}  // namespace codec
}  // namespace video
