// yuv_convert.h
#pragma once

#include "core/status.h"
#include "core/types.h"

namespace video {
namespace codec {
namespace utils {

// Convert `src` into `dst_format`, filling `dst` (planes, stride, format,
// timestamp). Supported v1: kI420 <-> kNV12. A same-format request copies the
// source. Any other pair returns StatusCode::kUnsupportedFormat.
StatusCode ConvertPixelFormat(const VideoFrame& src, PixelFormat dst_format,
                              VideoFrame& dst);

}  // namespace utils
}  // namespace codec
}  // namespace video
