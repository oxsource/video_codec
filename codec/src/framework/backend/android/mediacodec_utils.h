// mediacodec_utils.h
//
// Pure helpers shared by the Android MediaCodec backends (encoders + muxer).
// Deliberately free of any AMediaCodec/NDK dependency so these functions can
// be unit-tested on the host build (see tests/utils/mediacodec_utils_test.cc)
// and reused by the cross-compiled Android code.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "types.h"

namespace video {
namespace codec {
namespace android {

// AMediaCodec COLOR_Format_* value for a framework PixelFormat; 0 = unsupported
// (I420 -> COLOR_FormatYUV420Planar, NV12 -> COLOR_FormatYUV420SemiPlanar).
int ColorFormatFor(PixelFormat format);

// MediaCodec MIME type for a codec type; nullptr = unsupported.
const char* MimeFor(VideoCodecType codec);
const char* MimeFor(AudioCodecType codec);

// Bytes needed to hold one frame at width x height in `format`; 0 = invalid.
size_t BufferSizeFor(int width, int height, PixelFormat format);

// Append `payload` as one Annex-B access unit (4-byte start code + payload).
// When `keyframe`, prepend SPS/PPS (if non-empty) so the unit is self-
// contained. `out` is appended to, never cleared.
void AppendAnnexB(bool keyframe, const std::vector<uint8_t>& sps,
                  const std::vector<uint8_t>& pps, const std::vector<uint8_t>& payload,
                  std::vector<uint8_t>* out);

// Split an Annex-B byte stream (3/4-byte start-code delimited) into NAL units
// with the start codes stripped. Returns true if at least one unit was found.
bool SplitAnnexB(const uint8_t* data, size_t size, std::vector<std::vector<uint8_t>>* units);

// Build the 2-byte AudioSpecificConfig (AAC-LC, ISO 14496-3) for a sample rate
// / channel count. Returns false for rates or layouts not representable.
bool BuildAudioSpecificConfig(int sample_rate, int channels, std::vector<uint8_t>* out);

}  // namespace android
}  // namespace codec
}  // namespace video
