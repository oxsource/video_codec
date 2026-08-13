// ffmpeg_encode_file.cc
//
// A complete, runnable example of the video_codec pipeline:
//
//   SMPTE color-bars generator -> FFmpegVideoEncoder (push mode)
//     -> PacketQueue -> PacketPump -> consumer -> file
//
// Generates a synthetic SMPTE-style color-bars clip (with a moving white line
// for motion), encodes it as H.264 at ~fps, paces itself to wall-clock time so
// the default run takes about `seconds` (default 5) seconds, and writes the
// output. By default the output is muxed into an MP4 container (Mp4Consumer);
// "--raw" writes a raw Annex-B H.264 elementary stream (FileSinkConsumer).
//
// Usage:
//   ffmpeg_encode_file [--raw] [output] [seconds]
//
//   --raw  write a raw Annex-B H.264 elementary stream (FileSinkConsumer);
//          appends ".h264" to the output path. Otherwise the output is muxed
//          into an MP4 container (Mp4Consumer), appending ".mp4".
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

#include "api/encoder_factory.h"
#include "api/video_encoder.h"
#include "consumer/file_sink_consumer.h"
#include "consumer/mp4_consumer.h"
#include "consumer/packet_consumer.h"
#include "io/file_byte_sink.h"
#include "queue/packet_queue.h"
#include "utils/media_file_format.h"
#include "utils/smpte_bars.h"

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
  out_path += raw ? video::codec::utils::MediaFileFormat::kH264
                  : video::codec::utils::MediaFileFormat::kMp4;

  const int width = 640;
  const int height = 480;
  const int fps = 30;
  const int frame_count = seconds * fps;

  video::codec::VideoEncoderConfig cfg;
  cfg.codec = video::codec::VideoCodecType::kH264;
  cfg.width = width;
  cfg.height = height;
  cfg.fps = fps;
  cfg.bitrate = 2'000'000;
  cfg.input_format = video::codec::PixelFormat::kI420;
  cfg.force_backend = video::codec::Backend::kFFmpeg;

  // Transport: encoder (push) -> bounded ring buffer -> consumer.
  video::codec::PacketQueue queue(64, video::codec::Backpressure::kBlock);

  // A consumer is transport-agnostic: default -> MP4 muxer (fragmented by
  // default) over a FileByteSink; "--raw" -> a raw Annex-B file. Swapping the
  // ByteSink (file / cloud stream / tee) is a one-line change.
  std::unique_ptr<video::codec::FileByteSink>
      mp4_sink;  // must outlive the consumer
  std::unique_ptr<video::codec::PacketConsumer> consumer;
  if (raw) {
    consumer = std::make_unique<video::codec::FileSinkConsumer>(out_path);
  } else {
    mp4_sink = std::make_unique<video::codec::FileByteSink>(out_path);
    consumer = std::make_unique<video::codec::Mp4Consumer>(mp4_sink.get(),
                                                           width, height, fps);
  }

  std::unique_ptr<video::codec::VideoEncoder> encoder =
      video::codec::CreateVideoEncoder(cfg);
  if (!encoder) {
    std::fprintf(stderr, "ffmpeg_encode_file: no FFmpeg backend available\n");
    return 1;
  }
  if (encoder->Init() != video::codec::Status::kOk) {
    std::fprintf(stderr, "ffmpeg_encode_file: encoder Init failed\n");
    return 1;
  }
  if (encoder->SetOutputSink(&queue) != video::codec::Status::kOk) {
    std::fprintf(stderr, "ffmpeg_encode_file: push mode unavailable\n");
    return 1;
  }

  // Consumer thread awaits packets from the queue into the output.
  std::thread pump([&] { queue.Await(*consumer); });

  const auto start = std::chrono::steady_clock::now();
  int64_t produced = 0;
  for (int i = 0; i < frame_count; ++i) {
    video::codec::VideoFrame frame =
        video::codec::utils::SmpteBars::MakeFrame(width, height, fps, i);
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
  // (multi-producer safety). Await finishes the file at EOS.
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
