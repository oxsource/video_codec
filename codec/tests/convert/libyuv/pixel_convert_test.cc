// pixel_convert_test.cc
#include "convert/libyuv/pixel_convert.h"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace {

VideoFrame MakeI420(int w, int h) {
  VideoFrame f;
  f.format = PixelFormat::kI420;
  f.width = w;
  f.height = h;
  const size_t ysz = static_cast<size_t>(w) * h;
  const size_t csz = ysz / 4;
  f.planes[0].resize(ysz);
  f.planes[1].resize(csz, 2);
  f.planes[2].resize(csz, 3);
  for (size_t i = 0; i < ysz; ++i) {
    f.planes[0][i] = static_cast<uint8_t>(i & 0xFF);
  }
  return f;
}

bool Equals(const VideoFrame& a, const VideoFrame& b) {
  return a.width == b.width && a.height == b.height && a.format == b.format &&
         a.planes[0] == b.planes[0] && a.planes[1] == b.planes[1] && a.planes[2] == b.planes[2];
}

TEST(PixelConverterTest, I420ToNV12RoundTrip) {
  VideoFrame src = MakeI420(16, 16);
  VideoFrame nv12;
  ASSERT_EQ(PixelConverter::Convert(src, PixelFormat::kNV12, nv12), Status::kOk);
  EXPECT_EQ(nv12.format, PixelFormat::kNV12);
  EXPECT_EQ(nv12.planes[1].size(), static_cast<size_t>(16) * 8);

  VideoFrame back;
  ASSERT_EQ(PixelConverter::Convert(nv12, PixelFormat::kI420, back), Status::kOk);
  EXPECT_TRUE(Equals(src, back)) << "I420->NV12->I420 must be bit-exact";
}

TEST(PixelConverterTest, SameFormatCopies) {
  VideoFrame src = MakeI420(8, 8);
  VideoFrame dst;
  ASSERT_EQ(PixelConverter::Convert(src, PixelFormat::kI420, dst), Status::kOk);
  EXPECT_TRUE(Equals(src, dst));
}

TEST(PixelConverterTest, UnsupportedPairReturnsError) {
  VideoFrame src = MakeI420(8, 8);
  src.format = PixelFormat::kRGBA;  // kRGBA <-> kI420 not supported in v1
  VideoFrame dst;
  EXPECT_EQ(PixelConverter::Convert(src, PixelFormat::kI420, dst), Status::kUnsupportedFormat);
}

TEST(PixelConverterTest, InvalidDimensions) {
  VideoFrame src = MakeI420(0, 8);
  VideoFrame dst;
  EXPECT_EQ(PixelConverter::Convert(src, PixelFormat::kNV12, dst), Status::kInvalidArgument);
}

}  // namespace
}  // namespace codec
}  // namespace video
