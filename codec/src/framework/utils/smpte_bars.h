// smpte_bars.h
//
// SMPTE 75% color-bars test-pattern generator (I420 frames) plus the matching
// TV test audio tone (interleaved S16 PCM). Reusable across encoder examples,
// unit tests, and any code that needs a synthetic A/V source. Video chroma is
// 4:2:0 sampled from each 2x2 luma block's top-left pixel; the optional
// moving white line near the bottom adds motion so inter-frame coding
// produces meaningful P-frames. The default audio is a continuous 1 kHz tone;
// a periodic beep (125 ms every second) is available via AudioOptions.
//
// The palette, frame factory, and tone generator live as static members of
// `SmpteBars` so the whole test-pattern concept is reachable from one named
// type.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "types.h"

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

  // Audio-generation options (TV test tone). Explicit ctor for the same
  // clang default-argument reason as `Options`.
  struct AudioOptions {
    explicit AudioOptions(bool beep = false, int sample_rate = 48000, int channels = 2,
                          int samples_per_frame = 1024, int tone_hz = 1000)
        : beep(beep),
          sample_rate(sample_rate),
          channels(channels),
          samples_per_frame(samples_per_frame),
          tone_hz(tone_hz) {}

    // false = continuous tone (default); true = periodic 1 kHz beep, 125 ms
    // on / 875 ms off each second.
    bool beep;
    int sample_rate;
    int channels;
    int samples_per_frame;  // AAC frame size (1024) to avoid encoder padding
    int tone_hz;
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
  inline static constexpr size_t kCount = sizeof(kPalette) / sizeof(kPalette[0]);

  // BT.601 limited-range RGB -> YUV (studio swing: Y 16..235, U/V 16..240).
  static void RgbToYuv(const Color& c, uint8_t& y, uint8_t& u, uint8_t& v);

  // Build one I420 color-bars frame. `frame_index` drives the timestamp
  // (frame_index * 1e6 / fps us) and the moving-line position. width/height
  // must be positive and even (4:2:0 needs width/2 x height/2 chroma); on
  // invalid dimensions an empty frame (width == 0) is returned.
  static VideoFrame MakeVideoFrame(int width, int height, int fps, int frame_index,
                                   const Options& opts = Options());

  // Build one interleaved S16 AudioFrame of the TV test tone. `frame_index`
  // drives the timestamp (frame_index * samples_per_frame * 1e6 / sample_rate
  // us) and, in beep mode, the on/off phase within the 1-second period. On
  // invalid options (non-positive rate/channels/frame size/tone, or a tone
  // above Nyquist) an empty frame (data empty) is returned.
  static AudioFrame MakeAudioFrame(int frame_index, const AudioOptions& opts = AudioOptions());

  // Audio/video pacing: tells the caller when an audio frame is due so the
  // audio clock stays aligned with a 1/fps video clock. Owns the audio frame
  // index passed to MakeAudioFrame, so the generator timestamp can never
  // drift from the pacing count.
  struct AudioPace {
    // `opts` fixes the generated frame's rate/frame size; `fps` is the video
    // frame rate the audio must keep pace with.
    explicit AudioPace(const AudioOptions& opts, int fps) : opts_(opts), fps_(fps) {}

    // If an audio frame is due to cover the span of video frame
    // `video_frame_index` (0-based), produce it in `*out` and advance the
    // internal counters; return true. Returns false when the next frame
    // would overshoot the video boundary (call again for the next video
    // frame).
    bool NextAudioFrame(int video_frame_index, AudioFrame* out) {
      const int64_t end_samples =
          (static_cast<int64_t>(video_frame_index) + 1) * opts_.sample_rate / fps_;
      if (generated_ + opts_.samples_per_frame > end_samples) return false;
      *out = MakeAudioFrame(static_cast<int>(produced_), opts_);
      generated_ += opts_.samples_per_frame;
      ++produced_;
      return true;
    }

    // Number of audio frames handed out so far (also the next MakeAudioFrame
    // index); useful for progress reporting.
    int64_t produced() const { return produced_; }

   private:
    AudioOptions opts_;
    int fps_;
    int64_t generated_ = 0;  // total samples handed out
    int64_t produced_ = 0;   // audio frames handed out
  };

  // Renders SMPTE color bars into a hardware input surface — the ANativeWindow*
  // returned by `VideoEncoder::CreateInputSurface()`. Draws via EGL/GLES (the
  // canonical input-surface consumer; the CPU ANativeWindow lock path is not
  // acquired by every vendor encoder, research R7) and sets the per-frame
  // timestamp via eglPresentationTimeANDROID. This header stays NDK-free:
  // `native_window` is the opaque ANativeWindow* as void*. The platform check
  // lives inside the class: on platforms/builds without input-surface support
  // Create() returns nullptr and RenderFrame() returns false — callers just
  // test the returned pointer and need no platform guards.
  class Surface {
   public:
    // Set up an EGL display/surface/context over `native_window` (an
    // ANativeWindow* from CreateInputSurface). Returns nullptr on unsupported
    // platforms (non-Android), a null handle, invalid dimensions, or EGL
    // setup failure.
    static std::unique_ptr<Surface> Create(void* native_window, int width, int height);

    ~Surface();
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    // Draw one SMPTE color-bars frame (moving white line driven by
    // frame_index/fps) and present it to the encoder's input surface with
    // `timestamp_us`. Returns false on unsupported platforms or a
    // GLES/present failure.
    bool RenderFrame(int frame_index, int fps, int64_t timestamp_us);

   private:
    struct Impl;
    explicit Surface(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
  };
};

}  // namespace utils
}  // namespace codec
}  // namespace video
