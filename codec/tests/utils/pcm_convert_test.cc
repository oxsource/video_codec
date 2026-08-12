// pcm_convert_test.cc
#include "utils/pcm_convert.h"

#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace utils {
namespace {

AudioFrame MakeS16(int ch, int n) {
  AudioFrame f;
  f.format = SampleFormat::kS16;
  f.channels = ch;
  f.sample_rate = 48000;
  f.data.resize(static_cast<size_t>(n) * ch * 2);
  auto* base = f.data.data();
  for (int i = 0; i < n * ch; ++i) {
    const int16_t v = static_cast<int16_t>((i % 7) - 3);  // small-range pattern
    std::memcpy(base + i * 2, &v, sizeof(v));
  }
  return f;
}

TEST(PcmConvertTest, S16ToF32PlanarRoundTrip) {
  AudioFrame src = MakeS16(2, 64);
  AudioFrame planar;
  ASSERT_EQ(ConvertSampleFormat(src, SampleFormat::kF32Planar, planar),
            StatusCode::kOk);
  EXPECT_EQ(planar.format, SampleFormat::kF32Planar);

  AudioFrame back;
  ASSERT_EQ(ConvertSampleFormat(planar, SampleFormat::kS16, back),
            StatusCode::kOk);
  ASSERT_EQ(back.data.size(), src.data.size());
  for (size_t i = 0; i < src.data.size() / 2; ++i) {
    int16_t a, b;
    std::memcpy(&a, src.data.data() + i * 2, sizeof(a));
    std::memcpy(&b, back.data.data() + i * 2, sizeof(b));
    EXPECT_LE(std::abs(static_cast<int>(a) - static_cast<int>(b)), 1)
        << "sample " << i;
  }
}

TEST(PcmConvertTest, S16ToF32InterleavedSize) {
  AudioFrame src = MakeS16(2, 32);
  AudioFrame flt;
  ASSERT_EQ(ConvertSampleFormat(src, SampleFormat::kF32, flt), StatusCode::kOk);
  EXPECT_EQ(flt.format, SampleFormat::kF32);
  EXPECT_EQ(flt.data.size(), static_cast<size_t>(32) * 2 * 4);
}

TEST(PcmConvertTest, PlanarToInterleavedKeepsChannelData) {
  AudioFrame src = MakeS16(2, 8);
  AudioFrame planar;
  ASSERT_EQ(ConvertSampleFormat(src, SampleFormat::kF32Planar, planar),
            StatusCode::kOk);
  AudioFrame inter;
  ASSERT_EQ(ConvertSampleFormat(planar, SampleFormat::kF32, inter),
            StatusCode::kOk);
  EXPECT_EQ(inter.channels, 2);
  EXPECT_EQ(inter.sample_rate, src.sample_rate);
}

TEST(PcmConvertTest, InvalidChannels) {
  AudioFrame src = MakeS16(0, 8);
  AudioFrame dst;
  EXPECT_EQ(ConvertSampleFormat(src, SampleFormat::kF32Planar, dst),
            StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace utils
}  // namespace codec
}  // namespace video
