// stride.cc
#include "stride.h"

namespace video {
namespace codec {
namespace utils {

namespace {
size_t BytesPerSample(SampleFormat fmt) {
  switch (fmt) {
    case SampleFormat::kS16:
    case SampleFormat::kS16Planar:
      return 2;
    case SampleFormat::kF32:
    case SampleFormat::kF32Planar:
      return 4;
  }
  return 0;
}
}  // namespace

size_t Stride::Row(int width, PixelFormat fmt, int plane) {
  if (width <= 0 || plane < 0 || plane > 2) return 0;
  switch (fmt) {
    case PixelFormat::kI420:
      // Y full width; U/V quarter width.
      return (plane == 0) ? static_cast<size_t>(width) : static_cast<size_t>(width) / 2;
    case PixelFormat::kNV12:
      // Y full width; UV interleaved -> width bytes per row.
      return static_cast<size_t>(width);
    case PixelFormat::kRGBA:
      return static_cast<size_t>(width) * 4;
  }
  return 0;
}

size_t Stride::Sample(int channels, SampleFormat fmt) {
  if (channels <= 0) return 0;
  const size_t bps = BytesPerSample(fmt);
  if (bps == 0) return 0;
  const bool planar = (fmt == SampleFormat::kS16Planar || fmt == SampleFormat::kF32Planar);
  return planar ? bps : bps * static_cast<size_t>(channels);
}

}  // namespace utils
}  // namespace codec
}  // namespace video
