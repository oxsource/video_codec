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
//   bazel run //src/examples:encode_and_push -- out.mp4 http://localhost:8080/whip 5

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

namespace vc = video::codec;
namespace vcu = video::codec::utils;
namespace vs = video::stream;

static constexpr const char* kLogTag = "encode_and_push";

// Custom PacketSink that writes to a muxer AND pushes to a stream.
class RecordAndPushSink : public vc::PacketSink {
 public:
  RecordAndPushSink(vc::PacketSink* muxer, vs::Stream* stream)
      : muxer_(muxer), stream_(stream) {}

  vc::Status Push(vc::VideoPacket&& pkt) override {
    std::printf("  [sink] Push video: %zu bytes, stream=%s\n", pkt.data.size(), stream_ ? "yes" : "no");
    if (stream_) stream_->SendVideo(pkt);
    return muxer_->Push(std::move(pkt));
  }

  vc::Status Push(vc::AudioPacket&& pkt) override {
    if (stream_) stream_->SendAudio(pkt);
    return muxer_->Push(std::move(pkt));
  }

  vc::Status Finish() override { return muxer_->Finish(); }

 private:
  vc::PacketSink* muxer_;
  vs::Stream* stream_;
};

int main(int argc, char** argv) {
  std::string out_path = "out/out.mp4";
  std::string whip_url = "http://localhost:8080/whip";
  int seconds = 5;

  int argi = 1;
  if (argi < argc) out_path = argv[argi++];
  if (argi < argc) whip_url = argv[argi++];
  if (argi < argc) seconds = std::atoi(argv[argi++]);
  if (seconds <= 0) seconds = 5;

  mkdir("out", 0755);
  if (out_path == "out/out.mp4" && argi == 1) {
    out_path = "out/out.mp4";
  }

  // Resolve output path relative to workspace root (bazel run changes cwd).
  const char* workspace = getenv("BUILD_WORKSPACE_DIRECTORY");
  if (workspace && out_path[0] != '/') {
    out_path = std::string(workspace) + "/" + out_path;
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
  scfg.backend_type = "webrtc";
  scfg.remote_url = whip_url;
  scfg.video_codec = "h264";
  scfg.audio_codec = "aac";
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
    } else {
      std::fprintf(stderr, "%s: stream Start failed (%s) — recording only\n", kLogTag,
                   vc::StatusToString(st));
    }
  } else {
    std::fprintf(stderr, "%s: stream Init failed (%s) — recording only\n", kLogTag,
                 vc::StatusToString(st));
  }

  // ---- Muxer: record to local MP4 ----
  auto mp4_sink = std::make_unique<vc::FileByteSink>(out_path);

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

  auto muxer = vc::CodecFactory::CreateMuxer(mux_cfg);
  if (!muxer) {
    std::fprintf(stderr, "%s: no muxer available\n", kLogTag);
    return 1;
  }
  muxer->SetOutput(mp4_sink.get());

  RecordAndPushSink sink(muxer.get(), stream_ok ? stream.get() : nullptr);

  // ---- Codec setup: encode SMPTE bars to H.264 + AAC ----
  vc::PacketQueue queue(64, vc::Backpressure::kBlock);

  vc::VideoConfig vcfg;
  vcfg.codec = vc::VideoCodecType::kH264;
  vcfg.width = width;
  vcfg.height = height;
  vcfg.fps = fps;
  vcfg.bitrate = 2'000'000;
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