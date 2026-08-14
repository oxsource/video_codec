// smpte_bars_test.cc
#include "smpte_bars.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace video {
namespace codec {
namespace utils {
namespace {

TEST(SmpteBarsTest, FrameLayoutAndTimestamp) {
  const VideoFrame f = SmpteBars::MakeVideoFrame(640, 480, 30, 42);
  EXPECT_EQ(f.format, PixelFormat::kI420);
  EXPECT_EQ(f.width, 640);
  EXPECT_EQ(f.height, 480);
  EXPECT_EQ(f.timestamp_us, 42 * 1'000'000 / 30);
  EXPECT_EQ(f.planes[0].size(), 640u * 480u);
  EXPECT_EQ(f.planes[1].size(), 320u * 240u);
  EXPECT_EQ(f.planes[2].size(), 320u * 240u);
}

TEST(SmpteBarsTest, TimestampAdvancesWithFrameIndex) {
  EXPECT_EQ(SmpteBars::MakeVideoFrame(64, 48, 30, 0).timestamp_us, 0);
  EXPECT_EQ(SmpteBars::MakeVideoFrame(64, 48, 30, 1).timestamp_us, 33'333);
  EXPECT_EQ(SmpteBars::MakeVideoFrame(64, 48, 30, 30).timestamp_us, 1'000'000);
}

TEST(SmpteBarsTest, InvalidDimensionsReturnEmptyFrame) {
  EXPECT_EQ(SmpteBars::MakeVideoFrame(0, 480, 30, 0).width, 0);
  EXPECT_EQ(SmpteBars::MakeVideoFrame(640, 0, 30, 0).width, 0);
  EXPECT_EQ(SmpteBars::MakeVideoFrame(641, 480, 30, 0).width, 0);  // odd width
  EXPECT_EQ(SmpteBars::MakeVideoFrame(640, 481, 30, 0).width, 0);  // odd height
  EXPECT_EQ(SmpteBars::MakeVideoFrame(640, 480, 0, 0).width, 0);   // fps <= 0
}

TEST(SmpteBarsTest, RgbToYuvWhiteIsBrightNeutral) {
  uint8_t y, u, v;
  SmpteBars::RgbToYuv(SmpteBars::kPalette[0], y, u, v);
  EXPECT_GT(y, 170);
  EXPECT_EQ(u, 128);
  EXPECT_EQ(v, 128);
}

TEST(SmpteBarsTest, WhiteBarIsBrightNeutral) {
  const VideoFrame f = SmpteBars::MakeVideoFrame(64, 32, 30, 0);
  // Leftmost pixel is the white bar: bright luma, neutral chroma.
  EXPECT_GT(f.planes[0][0], 170);
  EXPECT_EQ(f.planes[1][0], 128);
  EXPECT_EQ(f.planes[2][0], 128);
}

TEST(SmpteBarsTest, BarsChangeAcrossX) {
  const VideoFrame f = SmpteBars::MakeVideoFrame(64, 32, 30, 0);
  // Pixel 0 is the white bar, pixel 32 (mid-frame) is the green bar
  // (32 * 7 / 64 = 3): luma differs between the bars.
  EXPECT_NE(f.planes[0][0], f.planes[0][32]);
}

TEST(SmpteBarsTest, MovingLineSweepsBottomRow) {
  const int w = 640, h = 480, fps = 30;
  const VideoFrame f = SmpteBars::MakeVideoFrame(w, h, fps, 15);
  const int line_x = (15 * w / fps) % w;  // 320
  const int line_w = w / 40;              // 16
  const int line_top = h - h / 10;        // 432

  // Inside the line: near-white (235). Outside it, at the same row: a bar.
  EXPECT_EQ(f.planes[0][static_cast<size_t>(line_top) * w + line_x], 235);
  EXPECT_EQ(f.planes[0][static_cast<size_t>(h - 1) * w + line_x + line_w / 2], 235);
  EXPECT_LT(f.planes[0][static_cast<size_t>(line_top) * w], 200);
}

TEST(SmpteBarsTest, MovingLineCanBeDisabled) {
  const int w = 640, h = 480, fps = 30;
  const SmpteBars::Options opts{false};
  const VideoFrame f = SmpteBars::MakeVideoFrame(w, h, fps, 15, opts);
  const int line_x = (15 * w / fps) % w;
  const int line_w = w / 40;
  EXPECT_LT(f.planes[0][static_cast<size_t>(h - 1) * w + line_x + line_w / 2], 200);
}

TEST(SmpteBarsTest, Deterministic) {
  const VideoFrame a = SmpteBars::MakeVideoFrame(64, 48, 30, 7);
  const VideoFrame b = SmpteBars::MakeVideoFrame(64, 48, 30, 7);
  EXPECT_EQ(a.timestamp_us, b.timestamp_us);
  EXPECT_EQ(a.planes[0], b.planes[0]);
  EXPECT_EQ(a.planes[1], b.planes[1]);
  EXPECT_EQ(a.planes[2], b.planes[2]);
}

TEST(SmpteBarsTest, AudioLayoutAndTimestamp) {
  const AudioFrame f = SmpteBars::MakeAudioFrame(42);
  EXPECT_EQ(f.format, SampleFormat::kS16);
  EXPECT_EQ(f.sample_rate, 48000);
  EXPECT_EQ(f.channels, 2);
  EXPECT_EQ(f.data.size(), 1024u * 2u * 2u);  // samples * channels * S16
  EXPECT_EQ(f.timestamp_us, 42LL * 1024 * 1'000'000 / 48000);
}

TEST(SmpteBarsTest, AudioTimestampsAdvanceWithFrameIndex) {
  EXPECT_EQ(SmpteBars::MakeAudioFrame(0).timestamp_us, 0);
  EXPECT_EQ(SmpteBars::MakeAudioFrame(1).timestamp_us, 1024 * 1'000'000 / 48000);
  // 47 frames * 1024 samples = 48128 samples > 1 s: past one full second.
  EXPECT_EQ(SmpteBars::MakeAudioFrame(47).timestamp_us, 1'002'666);
}

TEST(SmpteBarsTest, ContinuousToneIsNonZeroAndStereoIdentical) {
  const AudioFrame f = SmpteBars::MakeAudioFrame(0);  // default = continuous tone
  ASSERT_FALSE(f.data.empty());
  const int16_t* p = reinterpret_cast<const int16_t*>(f.data.data());
  bool any = false;
  for (size_t i = 0; i < f.data.size() / sizeof(int16_t); ++i) {
    if (p[i] != 0) {
      any = true;
      break;
    }
  }
  EXPECT_TRUE(any) << "a continuous tone must contain non-zero samples";
  // Stereo interleave: both channels carry the same sample at each index.
  EXPECT_EQ(p[0], p[1]);
}

TEST(SmpteBarsTest, AudioToneDeterministic) {
  const AudioFrame a = SmpteBars::MakeAudioFrame(7);
  const AudioFrame b = SmpteBars::MakeAudioFrame(7);
  EXPECT_EQ(a.timestamp_us, b.timestamp_us);
  EXPECT_EQ(a.data, b.data);
}

TEST(SmpteBarsTest, BeepHasSilenceGaps) {
  const SmpteBars::AudioOptions beep(true);  // periodic 1 kHz beep
  // Frame 0 spans samples 0..1023 (inside the 125 ms on-phase); frame 6 spans
  // 6144..7167 (inside the off-phase, which runs 6000..47999 of each second).
  const AudioFrame on = SmpteBars::MakeAudioFrame(0, beep);
  const AudioFrame off = SmpteBars::MakeAudioFrame(6, beep);
  const int16_t* op = reinterpret_cast<const int16_t*>(on.data.data());
  const int16_t* fp = reinterpret_cast<const int16_t*>(off.data.data());
  bool on_any = false;
  bool off_any = false;
  for (size_t i = 0; i < on.data.size() / sizeof(int16_t); ++i) {
    if (op[i] != 0) on_any = true;
    if (fp[i] != 0) off_any = true;
  }
  EXPECT_TRUE(on_any) << "beep must sound during the on-phase";
  EXPECT_FALSE(off_any) << "beep must be silent during the off-phase";
}

TEST(SmpteBarsTest, InvalidAudioOptionsReturnEmptyFrame) {
  EXPECT_TRUE(SmpteBars::MakeAudioFrame(0, SmpteBars::AudioOptions(false, 0)).data.empty());
  EXPECT_TRUE(SmpteBars::MakeAudioFrame(0, SmpteBars::AudioOptions(false, 48000, 0)).data.empty());
  EXPECT_TRUE(
      SmpteBars::MakeAudioFrame(0, SmpteBars::AudioOptions(false, 48000, 2, 0)).data.empty());
  EXPECT_TRUE(SmpteBars::MakeAudioFrame(0, SmpteBars::AudioOptions(false, 48000, 2, 1024, 0))
                  .data.empty());
  // Tone above Nyquist (sample_rate / 2) is rejected too.
  EXPECT_TRUE(SmpteBars::MakeAudioFrame(0, SmpteBars::AudioOptions(false, 48000, 2, 1024, 30000))
                  .data.empty());
}

TEST(SmpteBarsTest, AudioPaceTracksVideoClock) {
  const SmpteBars::AudioOptions opts;  // 48000 Hz, 1024 samples/frame
  SmpteBars::AudioPace pace(opts, 30);  // 1600 samples per 1/30 s video frame
  AudioFrame af;

  // Video frame 0 spans samples 0..1600: one 1024-sample frame fits.
  EXPECT_TRUE(pace.NextAudioFrame(0, &af));
  EXPECT_EQ(af.timestamp_us, 0);
  EXPECT_FALSE(pace.NextAudioFrame(0, &af));  // 2048 > 1600: wait for frame 1
  EXPECT_EQ(pace.produced(), 1);

  // Video frame 1 ends at 3200: two more frames fit (2048, 3072).
  EXPECT_TRUE(pace.NextAudioFrame(1, &af));
  EXPECT_EQ(af.timestamp_us, 1024 * 1'000'000 / 48000);
  EXPECT_TRUE(pace.NextAudioFrame(1, &af));
  EXPECT_EQ(af.timestamp_us, 2 * 1024 * 1'000'000 / 48000);
  EXPECT_FALSE(pace.NextAudioFrame(1, &af));  // 4096 > 3200
  EXPECT_EQ(pace.produced(), 3);
}

TEST(SmpteBarsTest, AudioPaceCoversVideoDuration) {
  const SmpteBars::AudioOptions opts;  // 48000 Hz, 1024 samples/frame
  SmpteBars::AudioPace pace(opts, 30);
  AudioFrame af;
  for (int i = 0; i < 30; ++i) {  // 1 second of video
    while (pace.NextAudioFrame(i, &af)) {
    }
  }
  // Audio covers the full video span without overshooting it: within one
  // 1024-sample frame of the 48000-sample target.
  const int64_t total_samples = pace.produced() * 1024;
  EXPECT_LE(total_samples, 48000);
  EXPECT_GT(total_samples, 48000 - 1024);
}

}  // namespace
}  // namespace utils
}  // namespace codec
}  // namespace video
