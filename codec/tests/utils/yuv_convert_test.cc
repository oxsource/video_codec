// yuv_convert_test.cc
#include "utils/yuv_convert.h"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace utils {
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
         a.planes[0] == b.planes[0] && a.planes[1] == b.planes[1] &&
         a.planes[2] == b.planes[2];
}

TEST(YuvConvertTest, I420ToNV12RoundTrip) {
  VideoFrame src = MakeI420(16, 16);
  VideoFrame nv12;
  ASSERT_EQ(ConvertPixelFormat(src, PixelFormat::kNV12, nv12), StatusCode::kOk);
  EXPECT_EQ(nv12.format, PixelFormat::kNV12);
  EXPECT_EQ(nv12.planes[1].size(), static_cast<size_t>(16) * 8);

  VideoFrame back;
  ASSERT_EQ(ConvertPixelFormat(nv12, PixelFormat::kI420, back), StatusCode::kOk);
  EXPECT_TRUE(Equals(src, back)) << "I420->NV12->I420 must be bit-exact";
}

TEST(YuvConvertTest, SameFormatCopies) {
  VideoFrame src = MakeI420(8, 8);
  VideoFrame dst;
  ASSERT_EQ(ConvertPixelFormat(src, PixelFormat::kI420, dst), StatusCode::kOk);
  EXPECT_TRUE(Equals(src, dst));
}

TEST(YuvConvertTest, UnsupportedPairReturnsError) {
  VideoFrame src = MakeI420(8, 8);
  src.format = PixelFormat::kRGBA;  // kRGBA <-> kI420 not supported in v1
  VideoFrame dst;
  EXPECT_EQ(ConvertPixelFormat(src, PixelFormat::kI420, dst),
            StatusCode::kUnsupportedFormat);
}

TEST(YuvConvertTest, InvalidDimensions) {
  VideoFrame src = MakeI420(0, 8);
  VideoFrame dst;
  EXPECT_EQ(ConvertPixelFormat(src, PixelFormat::kNV12, dst),
            StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace utils
}  // namespace codec
}  // namespace video
