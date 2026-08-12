// pcm_convert.cc
#include "utils/pcm_convert.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace video {
namespace codec {
namespace utils {

namespace {
constexpr float kS16Scale = 32768.0f;

int BytesPerSample(SampleFormat fmt) {
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

bool IsPlanar(SampleFormat fmt) {
  return fmt == SampleFormat::kS16Planar || fmt == SampleFormat::kF32Planar;
}

// Decode any supported layout into per-channel float planes.
// `planes[c][i]` = sample i of channel c.
std::vector<std::vector<float>> Decode(const AudioFrame& f) {
  const int ch = f.channels;
  const int bps = BytesPerSample(f.format);
  const size_t frame_bytes = static_cast<size_t>(bps) * ch;
  const size_t n =
      frame_bytes == 0 ? 0 : f.data.size() / frame_bytes;
  const bool planar = IsPlanar(f.format);

  std::vector<std::vector<float>> out(ch, std::vector<float>(n, 0.0f));
  for (size_t i = 0; i < n; ++i) {
    for (int c = 0; c < ch; ++c) {
      const size_t idx = planar ? (static_cast<size_t>(c) * n + i)
                                : (i * static_cast<size_t>(ch) + c);
      const uint8_t* src = f.data.data() + idx * bps;
      float value = 0.0f;
      if (bps == 2) {
        int16_t s;
        std::memcpy(&s, src, sizeof(s));
        value = static_cast<float>(s) / kS16Scale;
      } else {
        float fl;
        std::memcpy(&fl, src, sizeof(fl));
        value = fl;
      }
      out[c][i] = value;
    }
  }
  return out;
}

// Encode per-channel float planes into the requested layout in `dst.data`.
void Encode(const std::vector<std::vector<float>>& planes, SampleFormat fmt,
            AudioFrame& dst) {
  const int ch = static_cast<int>(planes.size());
  const size_t n = planes.empty() ? 0 : planes[0].size();
  const int bps = BytesPerSample(fmt);
  const bool planar = IsPlanar(fmt);
  // Total bytes is the same whether interleaved or planar: n samples * ch * bps.
  dst.data.resize(n * static_cast<size_t>(bps) * static_cast<size_t>(ch));

  for (size_t i = 0; i < n; ++i) {
    for (int c = 0; c < ch; ++c) {
      const size_t idx = planar ? (static_cast<size_t>(c) * n + i)
                                : (i * static_cast<size_t>(ch) + c);
      uint8_t* dst_ptr = dst.data.data() + idx * bps;
      if (bps == 2) {
        const float s = std::max(-1.0f, std::min(1.0f, planes[c][i]));
        const int32_t v =
            static_cast<int32_t>(std::lroundf(s * (kS16Scale - 1.0f)));
        const int16_t s16 = static_cast<int16_t>(v);
        std::memcpy(dst_ptr, &s16, sizeof(s16));
      } else {
        std::memcpy(dst_ptr, &planes[c][i], sizeof(float));
      }
    }
  }
}
}  // namespace

StatusCode ConvertSampleFormat(const AudioFrame& src, SampleFormat dst_format,
                               AudioFrame& dst) {
  if (src.channels <= 0 || src.sample_rate <= 0) return StatusCode::kInvalidArgument;
  if (BytesPerSample(src.format) == 0 || BytesPerSample(dst_format) == 0)
    return StatusCode::kUnsupportedFormat;

  const auto planes = Decode(src);
  dst.format = dst_format;
  dst.sample_rate = src.sample_rate;
  dst.channels = src.channels;
  dst.timestamp_us = src.timestamp_us;
  Encode(planes, dst_format, dst);
  return StatusCode::kOk;
}

}  // namespace utils
}  // namespace codec
}  // namespace video
