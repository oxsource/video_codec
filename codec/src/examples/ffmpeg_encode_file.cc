// ffmpeg_encode_file.cc
//
// A complete, runnable example of the video_codec pipeline:
//
//   SMPTE color-bars generator -> FFmpegVideoEncoder (push mode)
//     -> EncodedPacketQueue -> PacketPump -> FileSinkConsumer -> .h264 file
//
// Generates a synthetic SMPTE-style color-bars clip (with a moving white line
// for motion), encodes it as H.264 at ~fps, paces itself to wall-clock time so
// the default run takes about `seconds` (default 5) seconds, and writes a valid
// Annex-B H.264 elementary stream to the output file.
//
// Usage:
//   ffmpeg_encode_file [output.h264] [seconds]
//
// Example:
//   bazel run //src/examples:ffmpeg_encode_file -- out.h264 5

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "api/encoder_factory.h"
#include "api/video_encoder.h"
#include "consumer/file_sink_consumer.h"
#include "consumer/packet_pump.h"
#include "queue/encoded_packet_queue.h"

namespace vc = video::codec;

namespace {

struct RGB {
  int r, g, b;
};

// SMPTE 75% color bars (BT.601 limited-range approximation). Left to right.
const RGB kSmpteBars[] = {
    {191, 191, 191},  // white
    {191, 191, 0},    // yellow
    {0, 191, 191},    // cyan
    {0, 191, 0},      // green
    {191, 0, 191},    // magenta
    {191, 0, 0},      // red
    {0, 0, 191},      // blue
};
constexpr int kBarCount = sizeof(kSmpteBars) / sizeof(kSmpteBars[0]);

void RgbToYuv(const RGB& c, uint8_t& y, uint8_t& u, uint8_t& v) {
  const float r = static_cast<float>(c.r);
  const float g = static_cast<float>(c.g);
  const float b = static_cast<float>(c.b);
  y = static_cast<uint8_t>(16 + 0.257f * r + 0.504f * g + 0.098f * b);
  u = static_cast<uint8_t>(128 - 0.148f * r - 0.291f * g + 0.439f * b);
  v = static_cast<uint8_t>(128 + 0.439f * r - 0.368f * g - 0.071f * b);
}

// I420 (planar YUV420P) SMPTE color bars, one frame. `frame_index` drives the
// moving white line near the bottom (motion => meaningful P-frames).
vc::VideoFrame MakeColorBarsFrame(int w, int h, int fps, int frame_index) {
  vc::VideoFrame f;
  f.format = vc::PixelFormat::kI420;
  f.width = w;
  f.height = h;
  f.timestamp_us = static_cast<int64_t>(frame_index) * 1'000'000 / fps;

  const size_t ysz = static_cast<size_t>(w) * h;
  const size_t csz = static_cast<size_t>(w / 2) * (h / 2);
  f.planes[0].assign(ysz, 0);
  f.planes[1].assign(csz, 128);
  f.planes[2].assign(csz, 128);

  // Moving white line position (sweeps left->right over ~fps frames).
  const int line_x = (frame_index * w / fps) % w;
  const int line_w = w / 40;
  const int line_top = h - h / 10;

  for (int y = 0; y < h; ++y) {
    const bool in_line_row = y >= line_top;
    for (int x = 0; x < w; ++x) {
      int bar = x * kBarCount / w;
      if (bar >= kBarCount) bar = kBarCount - 1;
      uint8_t Y, U, V;
      RgbToYuv(kSmpteBars[bar], Y, U, V);
      if (in_line_row && x >= line_x && x < line_x + line_w) {
        Y = 235;  // white line
      }
      f.planes[0][static_cast<size_t>(y) * w + x] = Y;
      (void)U;
      (void)V;
    }
  }

  // Chroma (4:2:0): use the bar color of each 2x2 block's top-left luma pixel.
  for (int y = 0; y < h / 2; ++y) {
    for (int x = 0; x < w / 2; ++x) {
      const int px = x * 2;
      int bar = px * kBarCount / w;
      if (bar >= kBarCount) bar = kBarCount - 1;
      uint8_t Y, U, V;
      RgbToYuv(kSmpteBars[bar], Y, U, V);
      const size_t idx = static_cast<size_t>(y) * (w / 2) + x;
      f.planes[1][idx] = U;
      f.planes[2][idx] = V;
    }
  }
  return f;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string out_path = argc > 1 ? argv[1] : "out.h264";
  int seconds = argc > 2 ? std::atoi(argv[2]) : 5;
  if (seconds <= 0) seconds = 5;

  const int width = 640;
  const int height = 480;
  const int fps = 30;
  const int frame_count = seconds * fps;

  vc::VideoEncoderConfig cfg;
  cfg.codec = vc::VideoCodecType::kH264;
  cfg.width = width;
  cfg.height = height;
  cfg.fps = fps;
  cfg.bitrate = 2'000'000;
  cfg.input_format = vc::PixelFormat::kI420;
  cfg.force_backend = vc::Backend::kFFmpeg;

  // Transport: encoder (push) -> bounded ring buffer -> file sink.
  vc::EncodedPacketQueue queue(64, vc::Backpressure::kBlock);
  vc::FileSinkConsumer sink(out_path);

  std::unique_ptr<vc::VideoEncoder> encoder = vc::CreateVideoEncoder(cfg);
  if (!encoder) {
    std::fprintf(stderr, "ffmpeg_encode_file: no FFmpeg backend available\n");
    return 1;
  }
  if (encoder->Init() != vc::StatusCode::kOk) {
    std::fprintf(stderr, "ffmpeg_encode_file: encoder Init failed\n");
    return 1;
  }
  if (encoder->SetOutputSink(&queue) != vc::StatusCode::kOk) {
    std::fprintf(stderr, "ffmpeg_encode_file: push mode unavailable\n");
    return 1;
  }

  // Consumer thread drains the queue into the file.
  std::thread pump([&] { vc::PacketPump::Run(queue, sink); });

  const auto start = std::chrono::steady_clock::now();
  int64_t produced = 0;
  for (int i = 0; i < frame_count; ++i) {
    vc::VideoFrame frame = MakeColorBarsFrame(width, height, fps, i);
    const auto r = encoder->Encode(frame);
    if (!r.ok()) {
      std::fprintf(stderr, "ffmpeg_encode_file: Encode error %d at frame %d\n",
                   static_cast<int>(r.status()), i);
      break;
    }
    ++produced;
    // Pace to wall-clock ~fps so the default run takes ~`seconds`.
    std::this_thread::sleep_for(std::chrono::microseconds(1'000'000 / fps));
  }

  // Flush drains any remaining packets, then the CALLER marks end-of-stream
  // (multi-producer safety). PacketPump finishes the file at EOS.
  encoder->Flush();
  queue.MarkEos();
  pump.join();

  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  std::printf("ffmpeg_encode_file: encoded %lld frames in %lld ms -> %s\n",
              static_cast<long long>(produced),
              static_cast<long long>(elapsed_ms), out_path.c_str());
  return 0;
}
