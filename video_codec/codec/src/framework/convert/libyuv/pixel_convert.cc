// pixel_convert.cc
#include "codec/src/framework/convert/libyuv/pixel_convert.h"

#include <cstddef>

#include "libyuv/convert.h"
#include "codec/src/framework/utils/stride.h"

namespace video {
namespace codec {

namespace {

// Per-row stride for a plane; falls back to the natural tight layout when the
// caller left the VideoFrame.stride entry as 0.
int PlaneStride(const VideoFrame& f, int plane) {
  return f.stride[plane] > 0 ? f.stride[plane]
                             : static_cast<int>(utils::Stride::Row(f.width, f.format, plane));
}

}  // namespace

Status PixelConverter::Convert(const VideoFrame& src, PixelFormat dst_format, VideoFrame& dst) {
  if (src.width <= 0 || src.height <= 0) return Status::kInvalidArgument;
  if (src.format == dst_format) {
    dst = src;  // same-format request: plain copy
    return Status::kOk;
  }
  const bool to_nv12 = (src.format == PixelFormat::kI420 && dst_format == PixelFormat::kNV12);
  const bool to_i420 = (src.format == PixelFormat::kNV12 && dst_format == PixelFormat::kI420);
  if (!to_nv12 && !to_i420) return Status::kUnsupportedFormat;

  const int w = src.width;
  const int h = src.height;
  const int cw = w / 2;
  const int ch = h / 2;

  dst.format = dst_format;
  dst.width = w;
  dst.height = h;
  dst.timestamp_us = src.timestamp_us;

  if (to_nv12) {
    // I420 -> NV12: interleave U and V into a single UV plane.
    dst.planes[0].resize(static_cast<size_t>(h) * w);
    dst.planes[1].resize(static_cast<size_t>(ch) * w);
    dst.planes[2].clear();
    dst.stride[0] = w;
    dst.stride[1] = w;
    dst.stride[2] = 0;
    libyuv::I420ToNV12(src.planes[0].data(), PlaneStride(src, 0), src.planes[1].data(),
                       PlaneStride(src, 1), src.planes[2].data(), PlaneStride(src, 2),
                       dst.planes[0].data(), w, dst.planes[1].data(), w, w, h);
  } else {
    // NV12 -> I420: deinterleave UV into separate U and V planes.
    dst.planes[0].resize(static_cast<size_t>(h) * w);
    dst.planes[1].resize(static_cast<size_t>(ch) * cw);
    dst.planes[2].resize(static_cast<size_t>(ch) * cw);
    dst.stride[0] = w;
    dst.stride[1] = cw;
    dst.stride[2] = cw;
    libyuv::NV12ToI420(src.planes[0].data(), PlaneStride(src, 0), src.planes[1].data(),
                       PlaneStride(src, 1), dst.planes[0].data(), w, dst.planes[1].data(), cw,
                       dst.planes[2].data(), cw, w, h);
  }
  return Status::kOk;
}

}  // namespace codec
}  // namespace video
