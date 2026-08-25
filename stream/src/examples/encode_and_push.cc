// encode_and_push.cc
//
// Demonstrates simultaneous recording (to file) and streaming (via WebRTC/WHIP).
// Generates SMPTE color bars + 1 kHz test tone using the codec module, encodes
// as H.264 + AAC, then splits the output: one copy goes to a local MP4 file,
// the other is pushed to a remote WHIP endpoint.
//
// Usage:
//   bazel run //src/examples:encode_and_push -- [output.mp4] [whip_url] [seconds]
//
// Examples:
//   bazel run //src/examples:encode_and_push -- out.mp4 http://localhost:8889/test/whip 5

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <thread>

#include "src/framework/api/codec_factory.h"
#include "src/framework/api/audio_encoder.h"
#include "src/framework/api/muxer.h"
#include "src/framework/api/video_encoder.h"
#include "src/framework/core/packet_sink.h"
#include "src/framework/io/file_byte_sink.h"
#include "src/framework/queue/packet_queue.h"
#include "src/framework/utils/smpte_bars.h"

#include "src/api/stream.h"
#include "src/api/stream_config.h"
#include "src/api/stream_utils.h"

namespace vc = video::codec;
namespace vcu = video::codec::utils;
namespace vs = video::stream;

static constexpr const char* kLogTag = "encode_and_push";

// Custom PacketSink that writes to a muxer AND pushes to a stream.
class RecordAndPushSink : public vc::PacketSink {
 public:
  RecordAndPushSink(vc::PacketSink* sink, vs::Stream* stream)
      : sink_(sink), stream_(stream) {}

  vc::Status Push(vc::VideoPacket&& pkt) override {
    std::printf("  [sink] Push video: %zu bytes, stream=%s\n", pkt.data.size(), stream_ ? "yes" : "no");
    if (stream_) stream_->SendVideo(pkt);
    return sink_->Push(std::move(pkt));
  }

  vc::Status Push(vc::AudioPacket&& pkt) override {
    if (stream_) stream_->SendAudio(pkt);
    return sink_->Push(std::move(pkt));
  }

  vc::Status Finish() override { return sink_->Finish(); }

 private:
  vc::PacketSink* sink_;
  vs::Stream* stream_;
};

// Null sink that discards all data (used with --no-record).
class NullSink : public vc::PacketSink {
 public:
  vc::Status Push(vc::VideoPacket&&) override { return vc::Status::kOk; }
  vc::Status Push(vc::AudioPacket&&) override { return vc::Status::kOk; }
  vc::Status Finish() override { return vc::Status::kOk; }
};

int main(int argc, char** argv) {
  std::string out_path = "out/out.mp4";
  std::string whip_url = "http://localhost:8889/whip";
  int seconds = 5;
  bool no_record = false;

  std::vector<std::string> positional;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--no-record") {
      no_record = true;
    } else if (arg == "--") {
      // Everything after -- is positional
      while (++i < argc) positional.push_back(argv[i]);
      break;
    } else {
      positional.push_back(arg);
    }
  }
  // Parse positional args: [output] [url] [seconds]
  if (positional.size() >= 1 && positional[0].find("://") != std::string::npos) {
    whip_url = positional[0];
  } else if (positional.size() >= 1) {
    out_path = positional[0];
  }
  if (positional.size() >= 2) {
    if (positional[1].find("://") != std::string::npos) {
      whip_url = positional[1];
    } else {
      seconds = std::atoi(positional[1].c_str());
    }
  }
  if (positional.size() >= 3) {
    seconds = std::atoi(positional[2].c_str());
  }
  if (seconds <= 0) seconds = 5;

  // Resolve output path relative to workspace root (bazel run changes cwd).
  const char* workspace = getenv("BUILD_WORKSPACE_DIRECTORY");
  if (workspace && out_path[0] != '/') {
    out_path = std::string(workspace) + "/" + out_path;
  }
  // Ensure the output directory exists.
  auto dir_pos = out_path.rfind('/');
  if (dir_pos != std::string::npos) {
    std::string dir = out_path.substr(0, dir_pos);
    mkdir(dir.c_str(), 0755);
  }
  std::printf("%s: output=%s, whip=%s, seconds=%d\n", kLogTag,
              out_path.c_str(), whip_url.c_str(), seconds);

  const int width = 640;
  const int height = 480;
  const int fps = 30;
  const int sample_rate = 48000;
  const int channels = 2;
  const int frame_count = seconds * fps;

  // ---- Stream setup: push to WHIP endpoint ----
  vs::StreamConfig scfg;
  scfg.backend_type = vs::kBackendWebRTC;
  scfg.remote_url = whip_url;
  scfg.video_codec = vs::kCodecH264;
  scfg.audio_codec = vs::kCodecAAC;
  scfg.initial_bitrate_kbps = 2000;
  scfg.resolution_width = width;
  scfg.resolution_height = height;
  scfg.framerate = fps;

  auto stream = vs::Stream::Create(scfg);
  if (!stream) {
    std::fprintf(stderr, "%s: failed to create stream\n", kLogTag);
    return 1;
  }

  bool stream_ok = false;
  stream->SetStatusCallback([](const vs::StreamStatus& s) {
    std::printf("  [stream] state=%d bitrate=%u rtt=%u loss=%.1f%%\n",
                static_cast<int>(s.state), s.bitrate_kbps, s.rtt_ms, s.packet_loss_pct);
  });

  auto st = stream->Init();
  if (st == vc::Status::kOk) {
    st = stream->Start();
    if (st == vc::Status::kOk) {
      stream_ok = true;
      // Send a tiny dummy SEI frame immediately so the remote server
      // (e.g. MediaMTX) doesn't hit its "no tracks" timeout while the
      // codec pipeline is still being set up.
      stream->SendVideo(vs::MakeSeiFrame());
    } else {
      std::fprintf(stderr, "%s: stream Start failed (%s) — recording only\n", kLogTag,
                   vc::StatusToString(st));
    }
  } else {
    std::fprintf(stderr, "%s: stream Init failed (%s) — recording only\n", kLogTag,
                 vc::StatusToString(st));
  }

  // ---- Muxer: record to local MP4 (unless --no-record) ----
  NullSink null_sink;
  std::unique_ptr<vc::FileByteSink> mp4_sink;
  std::unique_ptr<vc::Muxer> muxer;
  vc::PacketSink* record_sink = &null_sink;

  if (!no_record) {
    mp4_sink = std::make_unique<vc::FileByteSink>(out_path);

    vc::MuxerConfig mux_cfg;
    mux_cfg.format = vc::MuxFormat::kMp4;
    mux_cfg.fragmented = true;
    mux_cfg.width = width;
    mux_cfg.height = height;
    mux_cfg.fps = fps;
    mux_cfg.audio_codec = vc::AudioCodecType::kAAC;
    mux_cfg.sample_rate = sample_rate;
    mux_cfg.channels = channels;
    mux_cfg.backend = vc::Backend::kAuto;

    muxer = vc::CodecFactory::CreateMuxer(mux_cfg);
    if (!muxer) {
      std::fprintf(stderr, "%s: no muxer available\n", kLogTag);
      return 1;
    }
    muxer->SetOutput(mp4_sink.get());
    record_sink = muxer.get();
  }

  RecordAndPushSink sink(record_sink, stream_ok ? stream.get() : nullptr);

  // ---- Codec setup: encode SMPTE bars to H.264 + AAC ----
  vc::PacketQueue queue(64, vc::Backpressure::kBlock);

  vc::VideoConfig vcfg;
  vcfg.codec = vc::VideoCodecType::kH264;
  vcfg.width = width;
  vcfg.height = height;
  vcfg.fps = fps;
  vcfg.bitrate = 2'000'000;
  vcfg.gop_size = 30;
  vcfg.input_format = vc::PixelFormat::kI420;
  vcfg.backend = vc::Backend::kAuto;

  auto venc_res = vc::CodecFactory::CreateVideo(vcfg, &queue);
  if (!venc_res.ok()) {
    std::fprintf(stderr, "%s: video encoder unavailable (%s)\n", kLogTag,
                 vc::StatusToString(venc_res.status()));
    return 1;
  }
  auto video_encoder = venc_res.Release();

  vc::AudioConfig acfg;
  acfg.codec = vc::AudioCodecType::kAAC;
  acfg.sample_rate = sample_rate;
  acfg.channels = channels;
  acfg.bitrate = 128'000;
  acfg.backend = vc::Backend::kAuto;

  auto aenc_res = vc::CodecFactory::CreateAudio(acfg, &queue);
  if (!aenc_res.ok()) {
    std::fprintf(stderr, "%s: audio encoder unavailable (%s)\n", kLogTag,
                 vc::StatusToString(aenc_res.status()));
    return 1;
  }
  auto audio_encoder = aenc_res.Release();

  // ---- Drain thread: sink consumes encoded packets (records + pushes) ----
  const auto drain = [&] {
    queue.Await(sink);
  };
  std::thread worker(drain);

  // ---- Encode loop: generate SMPTE bars, encode ----
  const auto start = std::chrono::steady_clock::now();
  int64_t produced = 0;
  vcu::SmpteBars::AudioPace audio_pace(vcu::SmpteBars::AudioOptions(), fps);

  std::printf("%s: encoding %d frames to %s + pushing to %s\n", kLogTag,
              frame_count, out_path.c_str(), stream_ok ? whip_url.c_str() : "(skipped)");

  for (int i = 0; i < frame_count; ++i) {
    vc::VideoFrame frame = vcu::SmpteBars::MakeVideoFrame(width, height, fps, i);
    auto r = video_encoder->Encode(frame);
    if (!r.ok()) {
      std::fprintf(stderr, "%s: video Encode error at frame %d\n", kLogTag, i);
      break;
    }
    ++produced;

    if (audio_encoder) {
      vc::AudioFrame af;
      while (audio_pace.NextAudioFrame(i, &af)) {
        auto ar = audio_encoder->Encode(af);
        if (!ar.ok()) break;
      }
    }

    std::this_thread::sleep_for(std::chrono::microseconds(1'000'000 / fps));
  }

  // ---- Flush and finalize ----
  video_encoder->Flush();
  if (audio_encoder) audio_encoder->Flush();
  queue.MarkEos();
  worker.join();

  if (stream_ok) {
    stream->Stop();
  }
  stream->Release();

  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  std::printf("%s: done — %lld frames encoded in %lld ms -> %s\n", kLogTag,
              static_cast<long long>(produced), static_cast<long long>(elapsed_ms),
              out_path.c_str());
  return 0;
}