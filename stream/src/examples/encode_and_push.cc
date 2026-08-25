// encode_and_push.cc
//
// Demonstrates simultaneous recording (to file) and streaming (via WebRTC/WHIP).
// Generates SMPTE color bars + 1 kHz test tone using the codec module, encodes
// as H.264 + AAC, then splits the output: one copy goes to a local MP4 file,
// the other is pushed to a remote WHIP endpoint.
//
// Usage:
//   bazel run //src/examples:encode_and_push -- [options]
//
// Options:
//   --no-record       Skip writing to local file (push only)
//   -o, --output      Output file path (default: out/out.mp4)
//   --url, --whip     WHIP endpoint URL (default: http://localhost:8889/whip)
//   --seconds, --time Encoding duration in seconds (default: 5)
//
// Examples:
//   bazel run //src/examples:encode_and_push -- --no-record --url http://localhost:8889/whip --seconds 30
//   bazel run //src/examples:encode_and_push -- --output out.mp4 --url http://localhost:8889/whip

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <thread>

#include "src/framework/api/codec_factory.h"
#include "src/framework/api/audio_encoder.h"
#include "src/framework/api/muxer.h"
#include "src/framework/api/video_encoder.h"
#include "src/framework/core/log_slot.h"
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

struct Options {
  std::string out_path = "out/out.mp4";
  std::string whip_url = "http://localhost:8889/whip";
  int seconds = 5;
  bool no_record = false;
};

// ---- Argument parsing -------------------------------------------------------

static Options ParseArgs(int argc, char** argv) {
  Options opts;
  for (int i = 1; i < argc; i++) {
    std::string arg(argv[i]);
    if (arg == "--no-record") {
      opts.no_record = true;
    } else if (arg == "-o" || arg == "--output") {
      if (++i < argc) opts.out_path = argv[i];
    } else if (arg == "--url" || arg == "--whip") {
      if (++i < argc) opts.whip_url = argv[i];
    } else if (arg == "--seconds" || arg == "--time") {
      if (++i < argc) opts.seconds = std::atoi(argv[i]);
    } else if (arg.find("--") == 0) {
      VC_LOG(video::codec::LogLevel::kError, std::string("Unknown option: ") + arg);
      std::exit(1);
    }
  }
  if (opts.seconds <= 0) opts.seconds = 5;

  const char* workspace = getenv("BUILD_WORKSPACE_DIRECTORY");
  if (workspace && opts.out_path[0] != '/') {
    opts.out_path = std::string(workspace) + "/" + opts.out_path;
  }
  auto dir_pos = opts.out_path.rfind('/');
  if (dir_pos != std::string::npos) {
    std::string dir = opts.out_path.substr(0, dir_pos);
    mkdir(dir.c_str(), 0755);
  }

  VC_LOG(video::codec::LogLevel::kInfo,
         std::string("output=") + opts.out_path +
         " whip=" + opts.whip_url +
         " seconds=" + std::to_string(opts.seconds));
  return opts;
}

// ---- Encoder pipeline -------------------------------------------------------

struct EncoderPipeline {
  std::unique_ptr<vc::VideoEncoder> video;
  std::unique_ptr<vc::AudioEncoder> audio;
  std::unique_ptr<vc::PacketQueue> queue;
};

static std::unique_ptr<EncoderPipeline> SetupCodec(int width, int height, int fps,
                                                    int sample_rate, int channels) {
  auto p = std::make_unique<EncoderPipeline>();
  p->queue = std::make_unique<vc::PacketQueue>(64, vc::Backpressure::kBlock);

  vc::VideoConfig vcfg;
  vcfg.codec = vc::VideoCodecType::kH264;
  vcfg.width = width;
  vcfg.height = height;
  vcfg.fps = fps;
  vcfg.bitrate = 2'000'000;
  vcfg.gop_size = 30;
  vcfg.input_format = vc::PixelFormat::kI420;
  vcfg.backend = vc::Backend::kAuto;

  auto venc = vc::CodecFactory::CreateVideo(vcfg, p->queue.get());
  if (!venc.ok()) {
    VC_LOG(video::codec::LogLevel::kError, std::string("video encoder unavailable: ") + vc::StatusToString(venc.status()));
    std::exit(1);
  }
  p->video = venc.Release();

  vc::AudioConfig acfg;
  acfg.codec = vc::AudioCodecType::kAAC;
  acfg.sample_rate = sample_rate;
  acfg.channels = channels;
  acfg.bitrate = 128'000;
  acfg.backend = vc::Backend::kAuto;

  auto aenc = vc::CodecFactory::CreateAudio(acfg, p->queue.get());
  if (!aenc.ok()) {
    VC_LOG(video::codec::LogLevel::kError, std::string("audio encoder unavailable: ") + vc::StatusToString(aenc.status()));
    std::exit(1);
  }
  p->audio = aenc.Release();

  return p;
}

// ---- Muxer / sink -----------------------------------------------------------

class NullSink : public vc::PacketSink {
 public:
  vc::Status Push(vc::VideoPacket&&) override { return vc::Status::kOk; }
  vc::Status Push(vc::AudioPacket&&) override { return vc::Status::kOk; }
  vc::Status Finish() override { return vc::Status::kOk; }
};

class RecordAndPushSink : public vc::PacketSink {
 public:
  RecordAndPushSink(vc::PacketSink* sink, vs::Stream* stream)
      : sink_(sink), stream_(stream) {}

  vc::Status Push(vc::VideoPacket&& pkt) override {
    VC_LOG(video::codec::LogLevel::kDebug, std::string("sink Push video: ") + std::to_string(pkt.data.size()) + " bytes, stream=" + (stream_ ? "yes" : "no"));
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

static NullSink g_null_sink;  // global singleton — always available

struct MuxerResult {
  std::unique_ptr<vc::FileByteSink> file_sink;
  std::unique_ptr<vc::Muxer> muxer;
  vc::PacketSink* record_sink = &g_null_sink;
};

static MuxerResult SetupMuxer(const Options& opts, int width, int height,
                               int fps, int sample_rate, int channels) {
  MuxerResult m;

  if (!opts.no_record) {
    m.file_sink = std::make_unique<vc::FileByteSink>(opts.out_path);

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

    m.muxer = vc::CodecFactory::CreateMuxer(mux_cfg);
    if (!m.muxer) {
      VC_LOG(video::codec::LogLevel::kError, "no muxer available");
      std::exit(1);
    }
    m.muxer->SetOutput(m.file_sink.get());
    m.record_sink = m.muxer.get();
  }
  return m;
}

// ---- Stream setup -----------------------------------------------------------

static std::unique_ptr<vs::Stream> SetupStream(const std::string& whip_url,
                                                int width, int height, int fps) {
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
    VC_LOG(video::codec::LogLevel::kError, "failed to create stream");
    std::exit(1);
  }

  stream->SetStatusCallback([](const vs::StreamStatus& s) {
    VC_LOG(video::codec::LogLevel::kInfo,
           std::string("state=") + std::to_string(static_cast<int>(s.state)) +
           " bitrate=" + std::to_string(s.bitrate_kbps) +
           " rtt=" + std::to_string(s.rtt_ms) +
           " loss=" + std::to_string(s.packet_loss_pct) + "%");
  });

  auto st = stream->Init();
  if (st != vc::Status::kOk) {
    VC_LOG(video::codec::LogLevel::kError, std::string("stream Init failed: ") + vc::StatusToString(st));
    std::exit(1);
  }

  st = stream->Start();
  if (st != vc::Status::kOk) {
    VC_LOG(video::codec::LogLevel::kError, std::string("stream Start failed: ") + vc::StatusToString(st) + " — recording only");
    return stream;
  }

  // Send a dummy SEI frame so the remote server doesn't hit "no tracks" timeout
  // while the encoder pipeline is being set up.
  stream->SendVideo(vs::MakeSeiFrame());
  return stream;
}

// ---- Encode loop ------------------------------------------------------------

static int64_t RunEncodeLoop(vc::VideoEncoder& video, vc::AudioEncoder* audio,
                              int frame_count, int width, int height, int fps,
                              const std::string& out_path, const std::string& whip_url,
                              bool stream_ok) {
  const auto start = std::chrono::steady_clock::now();
  int64_t produced = 0;
  vcu::SmpteBars::AudioPace audio_pace(vcu::SmpteBars::AudioOptions(), fps);

  VC_LOG(video::codec::LogLevel::kInfo,
         std::string("encoding ") + std::to_string(frame_count) + " frames to " +
         out_path + " + pushing to " + (stream_ok ? whip_url : "(skipped)"));

  for (int i = 0; i < frame_count; ++i) {
    vc::VideoFrame frame = vcu::SmpteBars::MakeVideoFrame(width, height, fps, i);
    auto r = video.Encode(frame);
    if (!r.ok()) {
      VC_LOG(video::codec::LogLevel::kError, std::string("video Encode error at frame ") + std::to_string(i));
      break;
    }
    ++produced;

    if (audio) {
      vc::AudioFrame af;
      while (audio_pace.NextAudioFrame(i, &af)) {
        auto ar = audio->Encode(af);
        if (!ar.ok()) break;
      }
    }

    std::this_thread::sleep_for(std::chrono::microseconds(1'000'000 / fps));
  }

  video.Flush();
  if (audio) audio->Flush();

  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  VC_LOG(video::codec::LogLevel::kInfo,
         std::string("done — ") + std::to_string(produced) + " frames encoded in " +
         std::to_string(elapsed_ms) + " ms -> " + out_path);
  return produced;
}

// ---- Main -------------------------------------------------------------------

int main(int argc, char** argv) {
  auto opts = ParseArgs(argc, argv);

  const int width = 640;
  const int height = 480;
  const int fps = 30;
  const int sample_rate = 48000;
  const int channels = 2;
  const int frame_count = opts.seconds * fps;

  auto stream = SetupStream(opts.whip_url, width, height, fps);
  bool stream_ok = (stream->GetStatus().state == vs::StreamState::kStreaming);

  auto mux = SetupMuxer(opts, width, height, fps, sample_rate, channels);
  RecordAndPushSink sink(mux.record_sink, stream_ok ? stream.get() : nullptr);

  auto pipe = SetupCodec(width, height, fps, sample_rate, channels);

  std::thread drain([&] { pipe->queue->Await(sink); });

  RunEncodeLoop(*pipe->video, pipe->audio.get(), frame_count,
                width, height, fps, opts.out_path, opts.whip_url, stream_ok);

  pipe->queue->MarkEos();
  drain.join();

  if (stream_ok) stream->Stop();
  stream->Release();

  return 0;
}