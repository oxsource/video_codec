// smpte_bars.cc
#include "smpte_bars.h"

namespace video {
namespace codec {
namespace utils {

namespace {

// Bar index (0..SmpteBars::kCount-1) covering luma pixel `x` of a `width` row.
int BarAt(int x, int width) {
  const int bar =
      static_cast<int>(static_cast<int64_t>(x) * static_cast<int64_t>(SmpteBars::kCount) / width);
  if (bar >= static_cast<int>(SmpteBars::kCount)) {
    return static_cast<int>(SmpteBars::kCount) - 1;
  }
  return bar;
}

}  // namespace

void SmpteBars::RgbToYuv(const Color& c, uint8_t& y, uint8_t& u, uint8_t& v) {
  const float r = static_cast<float>(c.r);
  const float g = static_cast<float>(c.g);
  const float b = static_cast<float>(c.b);
  y = static_cast<uint8_t>(16 + 0.257f * r + 0.504f * g + 0.098f * b);
  u = static_cast<uint8_t>(128 - 0.148f * r - 0.291f * g + 0.439f * b);
  v = static_cast<uint8_t>(128 + 0.439f * r - 0.368f * g - 0.071f * b);
}

VideoFrame SmpteBars::MakeFrame(int width, int height, int fps, int frame_index,
                                const Options& opts) {
  VideoFrame f;
  if (width <= 0 || height <= 0 || width % 2 != 0 || height % 2 != 0 || fps <= 0) {
    return f;  // empty frame (width == 0) on invalid input
  }
  f.format = PixelFormat::kI420;
  f.width = width;
  f.height = height;
  f.timestamp_us = static_cast<int64_t>(frame_index) * 1'000'000 / fps;

  const size_t ysz = static_cast<size_t>(width) * height;
  const size_t csz = static_cast<size_t>(width / 2) * (height / 2);
  f.planes[0].assign(ysz, 0);
  f.planes[1].assign(csz, 128);
  f.planes[2].assign(csz, 128);

  // Moving white line position (sweeps left->right over ~fps frames).
  const int line_x = (frame_index * width / fps) % width;
  const int line_w = width / 40;
  const int line_top = height - height / 10;

  for (int y = 0; y < height; ++y) {
    const bool in_line_row = opts.moving_line && y >= line_top;
    for (int x = 0; x < width; ++x) {
      uint8_t Y, U, V;
      RgbToYuv(kPalette[BarAt(x, width)], Y, U, V);
      if (in_line_row && x >= line_x && x < line_x + line_w) {
        Y = 235;  // white line
      }
      f.planes[0][static_cast<size_t>(y) * width + x] = Y;
      (void)U;
      (void)V;
    }
  }

  // Chroma (4:2:0): use the bar color of each 2x2 block's top-left luma pixel.
  for (int y = 0; y < height / 2; ++y) {
    for (int x = 0; x < width / 2; ++x) {
      const int px = x * 2;
      uint8_t Y, U, V;
      RgbToYuv(kPalette[BarAt(px, width)], Y, U, V);
      const size_t idx = static_cast<size_t>(y) * (width / 2) + x;
      f.planes[1][idx] = U;
      f.planes[2][idx] = V;
      (void)Y;
    }
  }
  return f;
}

}  // namespace utils
}  // namespace codec
}  // namespace video
