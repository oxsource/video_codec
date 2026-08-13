// pixel_convert.h
#pragma once

#include "status.h"
#include "types.h"

namespace video {
namespace codec {

// Pixel-format conversion for video frames, backed by Google libyuv. The class
// is cross-platform (Android/Linux/macOS/Windows — libyuv is a neutral
// dependency, unlike the FFmpeg-bound audio converter). All members are
// static (mirrors utils::SmpteBars).
class PixelConverter {
 public:
  // Convert `src` into `dst_format`, filling `dst` (planes, stride, format,
  // timestamp). Supported v1: kI420 <-> kNV12. A same-format request copies
  // the source. Any other pair returns Status::kUnsupportedFormat.
  static Status Convert(const VideoFrame& src, PixelFormat dst_format, VideoFrame& dst);
};

}  // namespace codec
}  // namespace video
