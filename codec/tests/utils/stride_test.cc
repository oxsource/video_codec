// stride_test.cc
#include "src/framework/utils/stride.h"

#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace utils {
namespace {

TEST(StrideTest, VideoRowStride) {
  EXPECT_EQ(Stride::Row(16, PixelFormat::kI420, 0), 16u);
  EXPECT_EQ(Stride::Row(16, PixelFormat::kI420, 1), 8u);
  EXPECT_EQ(Stride::Row(16, PixelFormat::kI420, 2), 8u);
  EXPECT_EQ(Stride::Row(16, PixelFormat::kNV12, 0), 16u);
  EXPECT_EQ(Stride::Row(16, PixelFormat::kNV12, 1), 16u);
  EXPECT_EQ(Stride::Row(16, PixelFormat::kRGBA, 0), 64u);
}

TEST(StrideTest, InvalidWidthOrPlane) {
  EXPECT_EQ(Stride::Row(0, PixelFormat::kI420, 0), 0u);
  EXPECT_EQ(Stride::Row(-4, PixelFormat::kNV12, 0), 0u);
  EXPECT_EQ(Stride::Row(16, PixelFormat::kI420, 3), 0u);
}

TEST(StrideTest, AudioSampleStride) {
  EXPECT_EQ(Stride::Sample(2, SampleFormat::kS16), 4u);
  EXPECT_EQ(Stride::Sample(2, SampleFormat::kF32), 8u);
  EXPECT_EQ(Stride::Sample(2, SampleFormat::kS16Planar), 2u);
  EXPECT_EQ(Stride::Sample(2, SampleFormat::kF32Planar), 4u);
  EXPECT_EQ(Stride::Sample(0, SampleFormat::kS16), 0u);
}

}  // namespace
}  // namespace utils
}  // namespace codec
}  // namespace video
