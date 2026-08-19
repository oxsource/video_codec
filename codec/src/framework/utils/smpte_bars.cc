// smpte_bars.cc
#include "src/framework/utils/smpte_bars.h"

#include <cmath>

#if defined(__ANDROID__)
// EGL window-surface management lives in `egl_surface`; only the SMPTE
// pattern drawing needs GLES here.
#include <GLES2/gl2.h>
#endif

#include "src/framework/utils/egl_surface.h"

namespace video {
namespace codec {
namespace utils {

namespace {

constexpr double kPi = 3.14159265358979323846;

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

VideoFrame SmpteBars::MakeVideoFrame(int width, int height, int fps, int frame_index,
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

AudioFrame SmpteBars::MakeAudioFrame(int frame_index, const AudioOptions& opts) {
  AudioFrame f;
  if (opts.sample_rate <= 0 || opts.channels <= 0 || opts.samples_per_frame <= 0 ||
      opts.tone_hz <= 0 || opts.tone_hz > opts.sample_rate / 2) {
    return f;  // empty frame (data empty) on invalid input
  }
  f.format = SampleFormat::kS16;
  f.sample_rate = opts.sample_rate;
  f.channels = opts.channels;
  f.timestamp_us = static_cast<int64_t>(frame_index) * opts.samples_per_frame * 1'000'000 /
                   opts.sample_rate;

  const int64_t start_sample = static_cast<int64_t>(frame_index) * opts.samples_per_frame;
  const int64_t beep_on = opts.sample_rate / 8;  // 125 ms on / 1 s period
  f.data.resize(static_cast<size_t>(opts.samples_per_frame) * opts.channels * sizeof(int16_t));
  auto* p = reinterpret_cast<int16_t*>(f.data.data());
  const double two_pi_f = 2.0 * kPi * opts.tone_hz / opts.sample_rate;
  for (int i = 0; i < opts.samples_per_frame; ++i) {
    const int64_t t = start_sample + i;
    const bool silent = opts.beep && (t % opts.sample_rate) >= beep_on;
    // ~1/4 full-scale amplitude (-12 dBFS) leaves headroom and never clips.
    const int16_t v = silent ? 0 : static_cast<int16_t>(8192.0 * std::sin(two_pi_f * t));
    for (int c = 0; c < opts.channels; ++c) {
      p[static_cast<size_t>(i) * opts.channels + c] = v;
    }
  }
  return f;
}

// ---- Surface (Android input-surface rendering) ---------------------
// The EGL window-surface machinery (display/surface/context, presentation
// time, swap) lives in the reusable `EglSurface` module; this class only draws
// the SMPTE pattern into the current EGL context and presents it.
#if defined(__ANDROID__)
struct SmpteBars::Surface::Impl {
  std::unique_ptr<EglSurface> egl;
};

std::unique_ptr<SmpteBars::Surface> SmpteBars::Surface::Create(void* native_window, int width, int height) {
  std::unique_ptr<EglSurface> egl = EglSurface::Create(native_window, width, height);
  if (!egl) return nullptr;
  auto impl = std::make_unique<Impl>();
  impl->egl = std::move(egl);
  return std::unique_ptr<Surface>(new Surface(std::move(impl)));
}

bool SmpteBars::Surface::RenderFrame(int frame_index, int fps, int64_t timestamp_us) {
  if (!impl_ || !impl_->egl || fps <= 0) return false;
  if (!impl_->egl->MakeCurrent()) return false;
  const int w = impl_->egl->Width();
  const int h = impl_->egl->Height();
  if (w <= 0 || h <= 0) return false;

  // 7 vertical SMPTE bars (kPalette, 75%) + a moving white line at the bottom
  // (matching MakeVideoFrame), drawn with scissor + clear — fixed-function
  // GLES2, no shaders.
  glViewport(0, 0, w, h);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  const int bar_w = (w + static_cast<int>(kCount) - 1) / static_cast<int>(kCount);
  for (size_t b = 0; b < kCount; ++b) {
    const int x0 = static_cast<int>(b) * bar_w;
    const int bw = b == kCount - 1 ? w - x0 : bar_w;
    glScissor(x0, 0, bw, h);
    glClearColor(kPalette[b].r / 255.0f, kPalette[b].g / 255.0f, kPalette[b].b / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
  }
  // Moving white line (bottom 10%, sweeping over ~fps frames) — matches the
  // CPU-path generator (MakeVideoFrame).
  const int line_x = (frame_index * w / fps) % w;
  const int line_w = w / 40;
  const int line_h = h / 10;
  glScissor(line_x, 0, line_w, line_h);
  glClearColor(1, 1, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glDisable(GL_SCISSOR_TEST);

  // Set the per-frame timestamp (µs -> ns) and present.
  impl_->egl->SetPresentationTimeNs(timestamp_us * 1000);
  return impl_->egl->SwapBuffers();
}

SmpteBars::Surface::~Surface() = default;

#else  // !defined(__ANDROID__)

struct SmpteBars::Surface::Impl {};

std::unique_ptr<SmpteBars::Surface> SmpteBars::Surface::Create(void*, int, int) {
  return nullptr;
}

bool SmpteBars::Surface::RenderFrame(int, int, int64_t) { return false; }

SmpteBars::Surface::~Surface() = default;

#endif  // defined(__ANDROID__)

SmpteBars::Surface::Surface(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

}  // namespace utils
}  // namespace codec
}  // namespace video
