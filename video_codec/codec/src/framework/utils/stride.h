// stride.h
#pragma once

#include <cstddef>

#include "src/framework/core/types.h"

namespace video {
namespace codec {
namespace utils {

// Byte-layout helpers for video rows and audio sample-frames. All members are
// static: the class groups the layout math under one named type (mirrors
// utils::SmpteBars).
class Stride {
 public:
  // Bytes per row for plane `plane` of a video frame (0 = luma/primary).
  // Uses the natural (tightly-packed) layout: no extra row padding beyond what
  // the format requires. Returns 0 if width <= 0 or plane is out of range.
  static size_t Row(int width, PixelFormat fmt, int plane = 0);

  // Bytes per single sample-frame for audio (one sample across all channels):
  //   interleaved -> channels * bytes_per_sample
  //   planar      -> bytes_per_sample (each channel has its own plane)
  // Returns 0 if channels <= 0 or the format is unknown.
  static size_t Sample(int channels, SampleFormat fmt);
};

}  // namespace utils
}  // namespace codec
}  // namespace video
