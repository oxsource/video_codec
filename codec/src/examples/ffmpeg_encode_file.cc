// ffmpeg_encode_file.cc
//
// A complete, runnable example of the video_codec pipeline:
//
//   SMPTE color-bars generator -> VideoEncoder (push mode)
//     -> PacketQueue -> Muxer (or FileConsumer) -> file
//
// Generates a synthetic SMPTE-style color-bars clip (with a moving white line
// for motion), encodes it as H.264 at ~fps, paces itself to wall-clock time so
// the default run takes about `seconds` (default 5) seconds, and writes the
// output. By default the output is muxed into an MP4 container by a Muxer
// (FFmpeg backend, implements PacketSink so Await hands packets to it);
// "--raw" writes a raw Annex-B H.264 elementary stream (FileConsumer).
//
// Usage:
//   ffmpeg_encode_file [--raw] [output] [seconds]
//
//   --raw  write a raw Annex-B H.264 elementary stream (FileConsumer);
//          appends ".h264" to the output path. Otherwise the output is muxed
//          into an MP4 container (Muxer), appending ".mp4".
//
// Examples:
//   bazel run //src/examples:ffmpeg_encode_file --          # -> out.mp4
//   bazel run //src/examples:ffmpeg_encode_file -- clip 5   # -> clip.mp4
//   bazel run //src/examples:ffmpeg_encode_file -- --raw    # -> out.h264

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "api/codec_factory.h"
#include "api/muxer.h"
#include "api/video_encoder.h"
#include "consumer/file_consumer.h"
#include "consumer/packet_consumer.h"
#include "io/file_byte_sink.h"
#include "queue/packet_queue.h"
#include "utils/media_format.h"
#include "utils/smpte_bars.h"

namespace vc = video::codec;
namespace vcu = video::codec::utils;

int main(int argc, char** argv) {
  bool raw = false;
  std::string out_path = "out";  // extension is appended below from `raw`
  int seconds = 5;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--raw") {
      raw = true;
    } else if (out_path == "out" && seconds == 5 && arg[0] != '-') {
      out_path = arg;
    } else {
      seconds = std::atoi(arg.c_str());
    }
  }
  if (seconds <= 0) seconds = 5;

  // The mode owns the extension: --raw -> .h264, otherwise MP4 -> .mp4.
  out_path += raw ? vcu::MediaFormat::kH264 : vcu::MediaFormat::kMp4;

  const int width = 640;
  const int height = 480;
  const int fps = 30;
  const int frame_count = seconds * fps;

  vc::VideoConfig cfg;
  cfg.codec = vc::VideoCodecType::kH264;
  cfg.width = width;
  cfg.height = height;
  cfg.fps = fps;
  cfg.bitrate = 2'000'000;
  cfg.input_format = vc::PixelFormat::kI420;
  cfg.backend = vc::Backend::kFFmpeg;

  // Transport: encoder (push) -> bounded ring buffer -> sink.
  vc::PacketQueue queue(64, vc::Backpressure::kBlock);

  // The output side differs by mode only in wiring; the encoder config above
  // is shared and protocol-agnostic (it never mentions a container):
  //   default -> Muxer (implements PacketSink; Await hands packets straight
  //              to it, writing a fragmented MP4 through the ByteSink)
  //   --raw   -> FileConsumer writing a raw Annex-B file
  std::unique_ptr<vc::FileByteSink> mp4_sink;  // must outlive the muxer
  std::unique_ptr<vc::Muxer> muxer;
  std::unique_ptr<vc::PacketConsumer> consumer;
  if (raw) {
    consumer = std::make_unique<vc::FileConsumer>(out_path);
  } else {
    vc::MuxerConfig mux_cfg;
    mux_cfg.format = vc::MuxFormat::kMp4;
    mux_cfg.fragmented = true;
    mux_cfg.width = width;
    mux_cfg.height = height;
    mux_cfg.fps = fps;
    mux_cfg.backend = vc::Backend::kFFmpeg;
    muxer = vc::CodecFactory::CreateMuxer(mux_cfg);
    if (!muxer) {
      std::fprintf(stderr, "ffmpeg_encode_file: no FFmpeg muxer available\n");
      return 1;
    }
    mp4_sink = std::make_unique<vc::FileByteSink>(out_path);
    muxer->SetOutput(mp4_sink.get());
  }

  std::unique_ptr<vc::VideoEncoder> encoder = vc::CodecFactory::CreateVideo(cfg);
  if (!encoder) {
    std::fprintf(stderr, "ffmpeg_encode_file: no FFmpeg backend available\n");
    return 1;
  }
  if (encoder->Init() != vc::Status::kOk) {
    std::fprintf(stderr, "ffmpeg_encode_file: encoder Init failed\n");
    return 1;
  }
  if (encoder->SetOutputSink(&queue) != vc::Status::kOk) {
    std::fprintf(stderr, "ffmpeg_encode_file: push mode unavailable\n");
    return 1;
  }

  // Drain thread: Await delivers packets to the sink (muxer or raw consumer)
  // and finishes it at EOS. Both implement PacketSink, so the conditional
  // resolves to the common base PacketSink* and Await needs no branch.
  const auto drain = [&] {
    vc::PacketSink* sink = muxer ? static_cast<vc::PacketSink*>(muxer.get())
                                 : static_cast<vc::PacketSink*>(consumer.get());
    queue.Await(*sink);
  };
  std::thread worker(drain);

  const auto start = std::chrono::steady_clock::now();
  int64_t produced = 0;
  for (int i = 0; i < frame_count; ++i) {
    vc::VideoFrame frame = vcu::SmpteBars::MakeFrame(width, height, fps, i);
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
  // (multi-producer safety). Await finishes the sink at EOS.
  encoder->Flush();
  queue.MarkEos();
  worker.join();

  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  std::printf("ffmpeg_encode_file: encoded %lld frames in %lld ms -> %s\n",
              static_cast<long long>(produced), static_cast<long long>(elapsed_ms),
              out_path.c_str());
  return 0;
}
