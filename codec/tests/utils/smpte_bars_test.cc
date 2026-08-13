// smpte_bars_test.cc
#include "utils/smpte_bars.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace utils {
namespace {

TEST(SmpteBarsTest, FrameLayoutAndTimestamp) {
  const VideoFrame f = SmpteBars::MakeFrame(640, 480, 30, 42);
  EXPECT_EQ(f.format, PixelFormat::kI420);
  EXPECT_EQ(f.width, 640);
  EXPECT_EQ(f.height, 480);
  EXPECT_EQ(f.timestamp_us, 42 * 1'000'000 / 30);
  EXPECT_EQ(f.planes[0].size(), 640u * 480u);
  EXPECT_EQ(f.planes[1].size(), 320u * 240u);
  EXPECT_EQ(f.planes[2].size(), 320u * 240u);
}

TEST(SmpteBarsTest, TimestampAdvancesWithFrameIndex) {
  EXPECT_EQ(SmpteBars::MakeFrame(64, 48, 30, 0).timestamp_us, 0);
  EXPECT_EQ(SmpteBars::MakeFrame(64, 48, 30, 1).timestamp_us, 33'333);
  EXPECT_EQ(SmpteBars::MakeFrame(64, 48, 30, 30).timestamp_us, 1'000'000);
}

TEST(SmpteBarsTest, InvalidDimensionsReturnEmptyFrame) {
  EXPECT_EQ(SmpteBars::MakeFrame(0, 480, 30, 0).width, 0);
  EXPECT_EQ(SmpteBars::MakeFrame(640, 0, 30, 0).width, 0);
  EXPECT_EQ(SmpteBars::MakeFrame(641, 480, 30, 0).width, 0);  // odd width
  EXPECT_EQ(SmpteBars::MakeFrame(640, 481, 30, 0).width, 0);  // odd height
  EXPECT_EQ(SmpteBars::MakeFrame(640, 480, 0, 0).width, 0);   // fps <= 0
}

TEST(SmpteBarsTest, RgbToYuvWhiteIsBrightNeutral) {
  uint8_t y, u, v;
  SmpteBars::RgbToYuv(SmpteBars::kPalette[0], y, u, v);
  EXPECT_GT(y, 170);
  EXPECT_EQ(u, 128);
  EXPECT_EQ(v, 128);
}

TEST(SmpteBarsTest, WhiteBarIsBrightNeutral) {
  const VideoFrame f = SmpteBars::MakeFrame(64, 32, 30, 0);
  // Leftmost pixel is the white bar: bright luma, neutral chroma.
  EXPECT_GT(f.planes[0][0], 170);
  EXPECT_EQ(f.planes[1][0], 128);
  EXPECT_EQ(f.planes[2][0], 128);
}

TEST(SmpteBarsTest, BarsChangeAcrossX) {
  const VideoFrame f = SmpteBars::MakeFrame(64, 32, 30, 0);
  // Pixel 0 is the white bar, pixel 32 (mid-frame) is the green bar
  // (32 * 7 / 64 = 3): luma differs between the bars.
  EXPECT_NE(f.planes[0][0], f.planes[0][32]);
}

TEST(SmpteBarsTest, MovingLineSweepsBottomRow) {
  const int w = 640, h = 480, fps = 30;
  const VideoFrame f = SmpteBars::MakeFrame(w, h, fps, 15);
  const int line_x = (15 * w / fps) % w;  // 320
  const int line_w = w / 40;              // 16
  const int line_top = h - h / 10;        // 432

  // Inside the line: near-white (235). Outside it, at the same row: a bar.
  EXPECT_EQ(f.planes[0][static_cast<size_t>(line_top) * w + line_x], 235);
  EXPECT_EQ(f.planes[0][static_cast<size_t>(h - 1) * w + line_x + line_w / 2],
            235);
  EXPECT_LT(f.planes[0][static_cast<size_t>(line_top) * w], 200);
}

TEST(SmpteBarsTest, MovingLineCanBeDisabled) {
  const int w = 640, h = 480, fps = 30;
  const SmpteBars::Options opts{false};
  const VideoFrame f = SmpteBars::MakeFrame(w, h, fps, 15, opts);
  const int line_x = (15 * w / fps) % w;
  const int line_w = w / 40;
  EXPECT_LT(f.planes[0][static_cast<size_t>(h - 1) * w + line_x + line_w / 2],
            200);
}

TEST(SmpteBarsTest, Deterministic) {
  const VideoFrame a = SmpteBars::MakeFrame(64, 48, 30, 7);
  const VideoFrame b = SmpteBars::MakeFrame(64, 48, 30, 7);
  EXPECT_EQ(a.timestamp_us, b.timestamp_us);
  EXPECT_EQ(a.planes[0], b.planes[0]);
  EXPECT_EQ(a.planes[1], b.planes[1]);
  EXPECT_EQ(a.planes[2], b.planes[2]);
}

}  // namespace
}  // namespace utils
}  // namespace codec
}  // namespace video
