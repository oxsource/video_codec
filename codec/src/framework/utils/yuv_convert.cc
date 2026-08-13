// yuv_convert.cc
#include "utils/yuv_convert.h"

#include <cstddef>
#include <cstdint>

#include "utils/stride.h"

namespace video {
namespace codec {
namespace utils {

namespace {

// Per-row source stride for a plane; falls back to the natural tight layout
// when the caller left the VideoFrame.stride entry as 0.
size_t PlaneStride(const VideoFrame& f, int plane) {
  return f.stride[plane] > 0 ? static_cast<size_t>(f.stride[plane])
                             : RowStride(f.width, f.format, plane);
}

// Copy the luma plane (identical layout for I420 and NV12).
void CopyLuma(const VideoFrame& src, VideoFrame& dst) {
  const size_t rows = static_cast<size_t>(src.height);
  const size_t row_stride = PlaneStride(src, 0);
  dst.planes[0].assign(src.planes[0].data(),
                       src.planes[0].data() + rows * row_stride);
  dst.stride[0] = src.width;
}

}  // namespace

StatusCode ConvertPixelFormat(const VideoFrame& src, PixelFormat dst_format,
                              VideoFrame& dst) {
  if (src.width <= 0 || src.height <= 0) return StatusCode::kInvalidArgument;
  if (src.format == dst_format) {
    dst = src;  // same-format request: plain copy
    return StatusCode::kOk;
  }
  const bool to_nv12 =
      (src.format == PixelFormat::kI420 && dst_format == PixelFormat::kNV12);
  const bool to_i420 =
      (src.format == PixelFormat::kNV12 && dst_format == PixelFormat::kI420);
  if (!to_nv12 && !to_i420) return StatusCode::kUnsupportedFormat;

  dst.format = dst_format;
  dst.width = src.width;
  dst.height = src.height;
  dst.timestamp_us = src.timestamp_us;
  CopyLuma(src, dst);

  const size_t cw = static_cast<size_t>(src.width) / 2;
  const size_t ch = static_cast<size_t>(src.height) / 2;

  if (to_nv12) {
    // I420 -> NV12: interleave U and V into a single UV plane.
    const size_t u_row = PlaneStride(src, 1);
    const size_t v_row = PlaneStride(src, 2);
    dst.planes[1].resize(ch * static_cast<size_t>(src.width));
    dst.stride[1] = src.width;  // NV12 UV row = width bytes
    dst.planes[2].clear();
    dst.stride[2] = 0;
    for (size_t y = 0; y < ch; ++y) {
      const uint8_t* u = src.planes[1].data() + y * u_row;
      const uint8_t* v = src.planes[2].data() + y * v_row;
      uint8_t* uv = dst.planes[1].data() + y * static_cast<size_t>(src.width);
      for (size_t x = 0; x < cw; ++x) {
        uv[2 * x] = u[x];
        uv[2 * x + 1] = v[x];
      }
    }
  } else {
    // NV12 -> I420: deinterleave UV into separate U and V planes.
    const size_t uv_row = PlaneStride(src, 1);
    dst.planes[1].resize(ch * cw);
    dst.planes[2].resize(ch * cw);
    dst.stride[1] = static_cast<int>(cw);
    dst.stride[2] = static_cast<int>(cw);
    for (size_t y = 0; y < ch; ++y) {
      const uint8_t* uv = src.planes[1].data() + y * uv_row;
      uint8_t* u = dst.planes[1].data() + y * cw;
      uint8_t* v = dst.planes[2].data() + y * cw;
      for (size_t x = 0; x < cw; ++x) {
        u[x] = uv[2 * x];
        v[x] = uv[2 * x + 1];
      }
    }
  }
  return StatusCode::kOk;
}

}  // namespace utils
}  // namespace codec
}  // namespace video
