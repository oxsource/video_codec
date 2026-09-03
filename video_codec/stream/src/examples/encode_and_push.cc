// encode_and_push.cc
//
// Demonstrates encoding + streaming (via WebRTC/WHIP), with optional local
// recording (--record). Generates SMPTE color bars + 1 kHz test tone using the
// codec module, encodes as H.264 + AAC, and pushes it to a remote WHIP
// endpoint. By default no file is written; pass --record to also save the
// encoded media to a fragmented MP4 (out/out.mp4 unless -o overrides it).
//
// The stream-side configuration comes from a unified JSON file, whose schema
// (field keys and defaults) is owned by the stream module (see
// StreamConfig::LoadFromFile / ::ParseFromJson in //src/core:stream_core)
// — the example only supplies the content:
//   bazel run //src/examples:encode_and_push -- --config src/examples/stream_conf.json
//   bazel run //src/examples:encode_and_push -- --config src/examples/stream_conf.json --record --seconds 5
//
// The WHIP URL is derived inside the module from the signal host + path:
//   host + "/" + path + "/whip"
// e.g.  http://localhost:8889 + test  ->  http://localhost:8889/test/whip
//
// A ready-to-use sample lives at src/examples/stream_conf.json.

// Module log tag: identifies all VC_LOG output from this file (see
// log_slot.h). Must be defined before the first header include so the
// framework's LOG_TAG mechanism picks it up instead of defaulting to __FILE__.
#define LOG_TAG "encode_and_push"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <thread>

#include "codec/src/framework/api/codec_factory.h"
#include "codec/src/framework/api/audio_encoder.h"
#include "codec/src/framework/api/muxer.h"
#include "codec/src/framework/api/video_encoder.h"
#include "codec/src/framework/core/log_slot.h"
#include "codec/src/framework/core/packet_sink.h"
#include "codec/src/framework/io/file_byte_sink.h"
#include "codec/src/framework/queue/packet_queue.h"
#include "codec/src/framework/utils/smpte_bars.h"

#include "stream/src/api/stream.h"
#include "stream/src/api/stream_config.h"
#include "stream/src/api/stream_utils.h"

namespace vc = video::codec;
namespace vcu = video::codec::utils;
namespace vs = video::stream;

// Stderr log slot: the framework default is a no-op, which hides all VC_LOG
// output (including WHIP/cpp_network diagnostics). Install this in main() to
// make every log line visible when debugging.
class StderrLogSlot : public vc::LogSlot {
 public:
  void Write(vc::LogLevel level, const char* file, int line,
             const std::string& msg) override {
    const char* name = "?";
    switch (level) {
      case vc::LogLevel::kInfo: name = "INFO"; break;
      case vc::LogLevel::kDebug: name = "DEBUG"; break;
      case vc::LogLevel::kWarn: name = "WARN"; break;
      case vc::LogLevel::kError: name = "ERROR"; break;
    }
    fprintf(stderr, "[%s] %s:%d %s\n", name, file, line, msg.c_str());
    fflush(stderr);
  }
};

StderrLogSlot g_stderr_slot;

// ---- Example-local options ---------------------------------------------------
//
// Everything describing the *stream* (signal host/path, codec, bitrate, ICE,
// network/TLS) lives in the unified module JSON config (parsed by
// //src/core:stream_core). This struct only carries run/record
// knobs that are local to this example binary.

struct Options {
  std::string config_file = "stream/src/examples/stream_conf.json";
  std::string out_path = "out/out.mp4";
  int seconds = 5;
  // Recording is OFF by default: the example only pushes to the WHIP endpoint.
  // Pass --record to also write a fragmented MP4 of the encoded media to
  // out_path, e.g.:
  //   --record --seconds 5 -o out/out.mp4
  bool record = false;
};

static Options g_opts;

// Resolve a relative output path against the bazel runfiles workspace root so
// it behaves consistently whether invoked via `bazel run` or directly.
static std::string ResolvePath(std::string path) {
  if (path[0] == '/') return path;
  if (const char* ws = getenv("BUILD_WORKSPACE_DIRECTORY")) {
    return std::string(ws) + "/" + path;
  }
  return path;
}

// Extract the signal "path" (the trailing segment of the WHIP URL) so we can
// tell the user where to view the stream in a browser:
//   WHIP: host + "/" + path + "/whip"  ->  path
static std::string SignalPathFromWhip(const std::string& whip_url) {
  std::string base = whip_url;
  const std::string suffix = "/whip";
  if (base.size() >= suffix.size() &&
      base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
    base.resize(base.size() - suffix.size());
  }
  auto pos = base.rfind('/');
  return pos == std::string::npos ? base : base.substr(pos + 1);
}

// ---- CLI / config resolution -------------------------------------------------

static vs::StreamConfig ParseConfig(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    std::string arg(argv[i]);
    if (arg == "--config" || arg == "-c") {
      if (++i < argc) g_opts.config_file = argv[i];
    } else if (arg == "-o" || arg == "--output") {
      if (++i < argc) g_opts.out_path = argv[i];
    } else if (arg == "--seconds" || arg == "--time") {
      if (++i < argc) g_opts.seconds = std::atoi(argv[i]);
    } else if (arg == "--record" || arg == "--record=true" ||
               arg == "--no-record=false") {
      g_opts.record = true;
    } else if (arg == "--no-record" || arg == "--no-record=true" ||
               arg == "--record=false") {
      g_opts.record = false;
    } else if (arg.rfind("--record=", 0) == 0 ||
               arg.rfind("--no-record=", 0) == 0) {
      VC_LOG(video::codec::LogLevel::kError,
             "Unknown value for record flag: '" + arg +
             "' (expected --record, --no-record or --record=true|false)");
      std::exit(1);
    } else {
      VC_LOG(video::codec::LogLevel::kError,
             std::string("Unknown option: ") + arg +
             " (stream config comes from a JSON file, see --config)");
      std::exit(1);
    }
  }

  std::string config_path = ResolvePath(g_opts.config_file);
  auto res = vs::StreamConfig::LoadFromFile(config_path);
  if (!res.ok()) {
    VC_LOG(video::codec::LogLevel::kError,
           "failed to load stream config from " + config_path);
    std::exit(1);
  }
  auto scfg = res.Release();

  g_opts.out_path = ResolvePath(g_opts.out_path);
  auto dir_pos = g_opts.out_path.rfind('/');
  if (dir_pos != std::string::npos) {
    std::string dir = g_opts.out_path.substr(0, dir_pos);
    mkdir(dir.c_str(), 0755);
  }
  if (g_opts.seconds <= 0) g_opts.seconds = 15;

  VC_LOG(video::codec::LogLevel::kInfo,
         std::string("config=") + config_path +
         " output=" + g_opts.out_path +
         " whip=" + scfg.remote_url +
         " seconds=" + std::to_string(g_opts.seconds) +
         " record=" + (g_opts.record ? "yes" : "no"));
  return scfg;
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
#if 0
    VC_LOG(video::codec::LogLevel::kDebug, std::string("sink Push video: ") + std::to_string(pkt.data.size()) + " bytes, stream=" + (stream_ ? "yes" : "no"));
#endif
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

static MuxerResult SetupMuxer(int width, int height,
                               int fps, int sample_rate, int channels) {
  MuxerResult m;

  if (g_opts.record) {
    m.file_sink = std::make_unique<vc::FileByteSink>(g_opts.out_path);

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

static std::unique_ptr<vs::Stream> SetupStream(const vs::StreamConfig& scfg) {
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
    VC_LOG(video::codec::LogLevel::kError,
           std::string("stream Start failed: ") + vc::StatusToString(st) +
           (g_opts.record ? " — recording only" : " — encoder still runs"));
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

  const std::string rec_target =
      g_opts.record ? out_path : "(recording disabled)";
  VC_LOG(video::codec::LogLevel::kInfo,
         std::string("encoding ") + std::to_string(frame_count) + " frames to " +
         rec_target + " + pushing to " + (stream_ok ? whip_url : "(skipped)"));

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
         std::to_string(elapsed_ms) + " ms" +
         (g_opts.record ? " -> " + out_path : " (recording disabled)"));
  return produced;
}

// ---- Main -------------------------------------------------------------------

int main(int argc, char** argv) {
  vc::SetLogSlot(&g_stderr_slot);

  vs::StreamConfig scfg = ParseConfig(argc, argv);

  // MediaMTX exposes the WHIP stream to web browsers at https://localhost:8892/<path>.
  const std::string view_path = SignalPathFromWhip(scfg.remote_url);
  VC_LOG(video::codec::LogLevel::kDebug,
         "View the stream in a web browser (MediaMTX):\n"
         "  https://localhost:8892/" + view_path +
         "\nSee https://mediamtx.org/docs/read/web-browsers for details.");

  const int width = scfg.resolution_width;
  const int height = scfg.resolution_height;
  const int fps = scfg.framerate;
  const int sample_rate = 48000;
  const int channels = 2;
  const int frame_count = g_opts.seconds * fps;

  auto stream = SetupStream(scfg);
  bool stream_ok = (stream->GetStatus().state == vs::StreamState::kStreaming);

  auto mux = SetupMuxer(width, height, fps, sample_rate, channels);
  RecordAndPushSink sink(mux.record_sink, stream_ok ? stream.get() : nullptr);

  auto pipe = SetupCodec(width, height, fps, sample_rate, channels);

  std::thread drain([&] { pipe->queue->Await(sink); });

  RunEncodeLoop(*pipe->video, pipe->audio.get(), frame_count,
                width, height, fps, g_opts.out_path, scfg.remote_url, stream_ok);

  pipe->queue->MarkEos();
  drain.join();

  if (stream_ok) stream->Stop();
  stream->Release();

  return 0;
}