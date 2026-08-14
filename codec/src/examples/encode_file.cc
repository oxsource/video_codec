// encode_file.cc
//
// A complete, runnable example of the video_codec pipeline:
//
//   SMPTE color-bars generator -> VideoEncoder (push mode)
//   TV test tone (1 kHz)       -> AudioEncoder (push mode)
//     -> PacketQueue -> Muxer (or FileConsumer) -> file
//
// Generates a synthetic SMPTE-style color-bars clip (with a moving white line
// for motion) plus a continuous 1 kHz TV test tone, encodes them as H.264 +
// AAC at ~fps, paces itself to wall-clock time so the default run takes about
// `seconds` (default 5) seconds, and writes the output. The backend is
// selected with --backend (default `auto` = platform select: Android ->
// MediaCodec, host -> FFmpeg), so the same binary drives both the FFmpeg and
// the Android MediaCodec backends. By default the output is muxed into an MP4
// container by a Muxer (implements PacketSink so Await hands video and audio
// packets straight to it); "--raw" writes a raw Annex-B H.264 elementary
// stream (FileConsumer). The output file name reflects the resolved backend:
// <out>-<backend>.<ext> (e.g. out-ffmpeg.mp4, out-android.mp4).
//
// Usage:
//   encode_file [--raw] [--backend auto|android|ffmpeg] [output] [seconds]
//
//   --backend  select the encoder/muxer backend (default auto);
//              android/ffmpeg force a specific backend.
//   --raw      write a raw Annex-B H.264 elementary stream (FileConsumer);
//              appends ".h264" to the output path. Otherwise the output is
//              muxed into an MP4 container (Muxer), appending ".mp4".
//
// Examples:
//   bazel run //src/examples:encode_file --            # -> out-ffmpeg.mp4
//   bazel run //src/examples:encode_file -- clip 5     # -> clip-ffmpeg.mp4
//   bazel run //src/examples:encode_file -- --raw      # -> out-ffmpeg.h264
//   bazel run //src/examples:encode_file -- --backend android clip 3
//                                                      # Android -> clip-android.mp4

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "codec_factory.h"
#include "muxer.h"
#include "audio_encoder.h"
#include "video_encoder.h"
#include "file_consumer.h"
#include "packet_consumer.h"
#include "file_byte_sink.h"
#include "packet_queue.h"
#include "media_format.h"
#include "smpte_bars.h"

namespace vc = video::codec;
namespace vcu = video::codec::utils;

static constexpr const char* kLogTag = "encode_file";

int main(int argc, char** argv) {
  bool raw = false;
  bool out_set = false;
  std::string out_path = "out";  // backend suffix + extension appended below
  int seconds = 5;
  vc::Backend backend = vc::Backend::kAuto;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--raw") {
      raw = true;
    } else if (arg == "--backend" || arg.rfind("--backend=", 0) == 0) {
      std::string name;
      if (arg == "--backend") {
        if (i + 1 >= argc) {
          std::fprintf(stderr, "%s: --backend requires a value (auto|android|ffmpeg)\n", kLogTag);
          return 1;
        }
        name = argv[++i];
      } else {
        name = arg.substr(arg.find('=') + 1);
      }
      if (name == "auto") {
        backend = vc::Backend::kAuto;
      } else if (name == "android") {
        backend = vc::Backend::kAndroid;
      } else if (name == "ffmpeg") {
        backend = vc::Backend::kFFmpeg;
      } else {
        std::fprintf(stderr, "%s: unknown --backend '%s' (auto|android|ffmpeg)\n", kLogTag,
                     name.c_str());
        return 1;
      }
    } else if (!out_set && arg[0] != '-') {
      // First positional argument is the output path; later ones are seconds.
      out_path = arg;
      out_set = true;
    } else {
      seconds = std::atoi(arg.c_str());
    }
  }
  if (seconds <= 0) seconds = 5;

  // The mode and backend own the output name: <out>-<backend>.<ext>. The
  // resolved backend (auto -> platform select) is reflected in the file name
  // via the canonical BackendToString name (e.g. out-ffmpeg.mp4).
  const vc::Backend resolved = vc::CodecFactory::ResolveBackend(backend);
  const char* backend_tag = vc::BackendToString(resolved);
  out_path += "-";
  out_path += backend_tag;
  out_path += raw ? vcu::MediaFormat::kH264 : vcu::MediaFormat::kMp4;

  const int width = 640;
  const int height = 480;
  const int fps = 30;
  const int sample_rate = 48000;
  const int channels = 2;
  const int frame_count = seconds * fps;

  vc::VideoConfig cfg;
  cfg.codec = vc::VideoCodecType::kH264;
  cfg.width = width;
  cfg.height = height;
  cfg.fps = fps;
  cfg.bitrate = 2'000'000;
  cfg.input_format = vc::PixelFormat::kI420;
  cfg.backend = backend;

  // Transport: encoder (push) -> bounded ring buffer -> sink.
  vc::PacketQueue queue(64, vc::Backpressure::kBlock);

  // The output side differs by mode only in wiring; the encoder config above
  // is shared and protocol-agnostic (it never mentions a container):
  //   default -> Muxer (implements PacketSink; Await hands video AND audio
  //              packets straight to it, writing a fragmented MP4 with H.264
  //              + AAC tracks through the ByteSink)
  //   --raw   -> FileConsumer writing a raw Annex-B file (video only)
  std::unique_ptr<vc::ByteSink> mp4_sink;  // must outlive the muxer
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
    mux_cfg.audio_codec = vc::AudioCodecType::kAAC;
    mux_cfg.sample_rate = sample_rate;
    mux_cfg.channels = channels;
    mux_cfg.backend = backend;
    muxer = vc::CodecFactory::CreateMuxer(mux_cfg);
    if (!muxer) {
      std::fprintf(stderr, "%s: no muxer available for backend %s\n", kLogTag, backend_tag);
      return 1;
    }
    mp4_sink = std::make_unique<vc::FileByteSink>(out_path);
    muxer->SetOutput(mp4_sink.get());
  }

  // Video encoder: create + Init + push-mode wiring in one factory call. The
  // queue implements PacketSink, so it is handed straight to the factory; the
  // Result carries the encoder (value()) or the failing step's status.
  auto venc_res = vc::CodecFactory::CreateVideo(cfg, &queue);
  if (!venc_res.ok()) {
    std::fprintf(stderr, "%s: video encoder unavailable (%s)\n", kLogTag,
                 vc::StatusToString(venc_res.status()));
    return 1;
  }
  std::unique_ptr<vc::VideoEncoder> video_encoder = venc_res.Release();

  // Audio encoder: same push-mode wiring into the SAME queue. Only the muxed
  // path encodes audio (raw mode writes a video-only elementary stream).
  std::unique_ptr<vc::AudioEncoder> audio_encoder;
  if (!raw) {
    vc::AudioConfig acfg;
    acfg.codec = vc::AudioCodecType::kAAC;
    acfg.sample_rate = sample_rate;
    acfg.channels = channels;
    acfg.bitrate = 128'000;
    acfg.backend = backend;
    auto aenc_res = vc::CodecFactory::CreateAudio(acfg, &queue);
    if (!aenc_res.ok()) {
      std::fprintf(stderr, "%s: audio encoder unavailable (%s)\n", kLogTag,
                   vc::StatusToString(aenc_res.status()));
      return 1;
    }
    audio_encoder = aenc_res.Release();
  }

  // Drain thread: Await delivers every packet (video AND audio, drained
  // alternately from the two rings) to the sink — muxer or raw consumer —
  // and finishes it at EOS. Both implement PacketSink; PacketSink::Ptr
  // upcasts the concrete unique_ptr to the common base so Await needs no
  // branch.
  const auto drain = [&] {
    vc::PacketSink* sink = muxer ? vc::PacketSink::Ptr(muxer) : vc::PacketSink::Ptr(consumer);
    queue.Await(*sink);
  };
  std::thread worker(drain);

  const auto start = std::chrono::steady_clock::now();
  int64_t produced = 0;
  // The pacer turns each video frame's wall-clock span into the right number
  // of AAC frames (1024 samples @48 kHz) and owns the audio frame index.
  vcu::SmpteBars::AudioPace audio_pace(vcu::SmpteBars::AudioOptions(), fps);
  for (int i = 0; i < frame_count; ++i) {
    vc::VideoFrame frame = vcu::SmpteBars::MakeVideoFrame(width, height, fps, i);
    const auto r = video_encoder->Encode(frame);
    if (!r.ok()) {
      std::fprintf(stderr, "%s: Encode error %d at frame %d\n", kLogTag, static_cast<int>(r.status()), i);
      break;
    }
    ++produced;

    if (audio_encoder) {
      vc::AudioFrame af;
      while (audio_pace.NextAudioFrame(i, &af)) {
        const auto ar = audio_encoder->Encode(af);
        if (!ar.ok()) {
          std::fprintf(stderr, "%s: audio Encode error %d at frame %d\n", kLogTag, static_cast<int>(ar.status()), i);
          break;
        }
      }
    }
    // Pace to wall-clock ~fps so the default run takes ~`seconds`.
    std::this_thread::sleep_for(std::chrono::microseconds(1'000'000 / fps));
  }

  // Flush drains any remaining packets, then the CALLER marks end-of-stream
  // (multi-producer safety). Await finishes the sink at EOS.
  video_encoder->Flush();
  if (audio_encoder) audio_encoder->Flush();
  queue.MarkEos();
  worker.join();

  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
  std::printf("%s: encoded %lld frames + %lld audio frames in %lld ms -> %s\n", kLogTag,
              static_cast<long long>(produced), static_cast<long long>(audio_pace.produced()),
              static_cast<long long>(elapsed_ms), out_path.c_str());
  return 0;
}
