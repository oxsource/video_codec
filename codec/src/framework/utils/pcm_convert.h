// pcm_convert.h
#pragma once

#include "core/status.h"
#include "core/types.h"

namespace video {
namespace codec {
namespace utils {

// Convert audio `src` into `dst_format`, filling `dst` (single buffer per
// AudioFrame convention; planar layouts store channel 0, then channel 1, ...).
// Supported v1: any pair among kS16, kF32, kS16Planar, kF32Planar.
// Unknown formats return StatusCode::kUnsupportedFormat.
StatusCode ConvertSampleFormat(const AudioFrame& src, SampleFormat dst_format,
                               AudioFrame& dst);

}  // namespace utils
}  // namespace codec
}  // namespace video
