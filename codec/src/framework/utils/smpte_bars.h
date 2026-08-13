// smpte_bars.h
//
// SMPTE 75% color-bars test-pattern generator (I420 frames). Reusable across
// encoder examples, unit tests, and any code that needs a synthetic video
// source. Chroma is 4:2:0 sampled from each 2x2 luma block's top-left pixel;
// the optional moving white line near the bottom adds motion so inter-frame
// coding produces meaningful P-frames.
//
// The palette and frame factory live as static members of `SmpteBars` so the
// whole test-pattern concept is reachable from one named type.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/types.h"

namespace video {
namespace codec {
namespace utils {

class SmpteBars {
 public:
  // 8-bit RGB color.
  struct Color {
    uint8_t r, g, b;
  };

  // Frame-generation options.
  struct Options {
    // Explicit ctor (no default member initializer): a nested struct with a
    // default member initializer cannot be used as a default argument in the
    // enclosing class (clang evaluates its exception spec too early).
    Options() : moving_line(true) {}
    explicit Options(bool moving) : moving_line(moving) {}

    // Sweep a white line left->right across the bottom (~one period per fps
    // frames). Disable for a static pattern.
    bool moving_line;
  };

  // SMPTE 75% color bars (BT.601 limited-range approximation), left to right.
  inline static constexpr Color kPalette[] = {
      {191, 191, 191},  // white
      {191, 191, 0},    // yellow
      {0, 191, 191},    // cyan
      {0, 191, 0},      // green
      {191, 0, 191},    // magenta
      {191, 0, 0},      // red
      {0, 0, 191},      // blue
  };
  inline static constexpr size_t kCount =
      sizeof(kPalette) / sizeof(kPalette[0]);

  // BT.601 limited-range RGB -> YUV (studio swing: Y 16..235, U/V 16..240).
  static void RgbToYuv(const Color& c, uint8_t& y, uint8_t& u, uint8_t& v);

  // Build one I420 color-bars frame. `frame_index` drives the timestamp
  // (frame_index * 1e6 / fps us) and the moving-line position. width/height
  // must be positive and even (4:2:0 needs width/2 x height/2 chroma); on
  // invalid dimensions an empty frame (width == 0) is returned.
  static VideoFrame MakeFrame(int width, int height, int fps, int frame_index,
                              const Options& opts = Options());
};

}  // namespace utils
}  // namespace codec
}  // namespace video
