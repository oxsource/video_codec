// types.h
#pragma once

#include <cstdint>
#include <vector>

namespace video {
namespace codec {

// ---- Codec / format enums -------------------------------------------------

enum class VideoCodecType { kH264, kHEVC };
enum class AudioCodecType { kAAC, kOpus };
enum class PixelFormat { kI420, kNV12, kRGBA };
enum class SampleFormat { kS16, kF32, kS16Planar, kF32Planar };
enum class BitrateMode { kConstant, kVariable };

// Which backend understands a NativeBuffer / which backend to force.
enum class Backend { kAuto, kAndroid, kDarwin, kFFmpeg };

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
  Backend backend = Backend::kAuto;  // kAuto -> platform select

  // True when the config describes a muxable stream (dimensions set).
  // Backends call this to reject bad configs before doing any real work.
  bool IsValid() const { return width > 0 && height > 0; }
};

}  // namespace codec
}  // namespace video
