// swr_audio_convert.h
#pragma once

#include "core/status.h"
#include "core/types.h"

namespace video {
namespace codec {

// Sample-format conversion for PCM audio, backed by FFmpeg libswresample.
// FFmpeg-dependent: available where the FFmpeg static archive is linked
// (non-Android host builds). All members are static (mirrors utils::SmpteBars).
class SwrAudioConverter {
 public:
  // Bytes per single sample (one channel, one sample-frame) for `fmt`; 0 if
  // the format is unknown/unsupported.
  static int BytesPerSample(SampleFormat fmt);

  // True for planar layouts (channel 0 plane, then channel 1, ...).
  static bool IsPlanar(SampleFormat fmt);

  // Convert audio `src` into `dst_format`, filling `dst` (single buffer per
  // AudioFrame convention; planar layouts store channel 0, then channel 1,
  // ...). Supported v1: any pair among kS16, kF32, kS16Planar, kF32Planar.
  // Unknown formats return Status::kUnsupportedFormat.
  static Status Convert(const AudioFrame& src, SampleFormat dst_format, AudioFrame& dst);
};

}  // namespace codec
}  // namespace video
