// swr_audio_convert_test.cc
#include "codec/src/framework/backend/ffmpeg/swr/swr_audio_convert.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "gtest/gtest.h"

namespace video {
namespace codec {
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

TEST(SwrAudioConverterTest, S16ToF32PlanarRoundTrip) {
  AudioFrame src = MakeS16(2, 64);
  AudioFrame planar;
  ASSERT_EQ(SwrAudioConverter::Convert(src, SampleFormat::kF32Planar, planar), Status::kOk);
  EXPECT_EQ(planar.format, SampleFormat::kF32Planar);

  AudioFrame back;
  ASSERT_EQ(SwrAudioConverter::Convert(planar, SampleFormat::kS16, back), Status::kOk);
  ASSERT_EQ(back.data.size(), src.data.size());
  for (size_t i = 0; i < src.data.size() / 2; ++i) {
    int16_t a, b;
    std::memcpy(&a, src.data.data() + i * 2, sizeof(a));
    std::memcpy(&b, back.data.data() + i * 2, sizeof(b));
    EXPECT_LE(std::abs(static_cast<int>(a) - static_cast<int>(b)), 1) << "sample " << i;
  }
}

TEST(SwrAudioConverterTest, S16ToF32InterleavedSize) {
  AudioFrame src = MakeS16(2, 32);
  AudioFrame flt;
  ASSERT_EQ(SwrAudioConverter::Convert(src, SampleFormat::kF32, flt), Status::kOk);
  EXPECT_EQ(flt.format, SampleFormat::kF32);
  EXPECT_EQ(flt.data.size(), static_cast<size_t>(32) * 2 * 4);
}

TEST(SwrAudioConverterTest, PlanarToInterleavedKeepsChannelData) {
  AudioFrame src = MakeS16(2, 8);
  AudioFrame planar;
  ASSERT_EQ(SwrAudioConverter::Convert(src, SampleFormat::kF32Planar, planar), Status::kOk);
  AudioFrame inter;
  ASSERT_EQ(SwrAudioConverter::Convert(planar, SampleFormat::kF32, inter), Status::kOk);
  EXPECT_EQ(inter.channels, 2);
  EXPECT_EQ(inter.sample_rate, src.sample_rate);
}

TEST(SwrAudioConverterTest, InvalidChannels) {
  AudioFrame src = MakeS16(0, 8);
  AudioFrame dst;
  EXPECT_EQ(SwrAudioConverter::Convert(src, SampleFormat::kF32Planar, dst),
            Status::kInvalidArgument);
}

}  // namespace
}  // namespace codec
}  // namespace video
