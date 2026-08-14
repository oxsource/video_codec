// codec_factory.h
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "result.h"
#include "types.h"

namespace video {
namespace codec {

class VideoEncoder;
class AudioEncoder;
class Muxer;
class PacketSink;  // fwd-declared: `api` stays free of a `queue` dependency.

// Backend creator functions self-registered by each backend.
using VideoCreator = std::function<std::unique_ptr<VideoEncoder>(const VideoConfig&)>;
using AudioCreator = std::function<std::unique_ptr<AudioEncoder>(const AudioConfig&)>;
using MuxerCreator = std::function<std::unique_ptr<Muxer>(const MuxerConfig&)>;

// Central factory for all codec capabilities (encoders + muxer). Backends
// self-register here so `api` never depends on `backend/*`; the selected
// backend (wired by `public` via select()) runs its registration at static
// init. Static-class utility: all methods are static, no instances.
class CodecFactory {
 public:
  // Resolve which backend to instantiate given a backend request and the
  // current platform. kAuto -> platform select (non-Android -> FFmpeg; Android
  // -> MediaCodec; Apple falls back to FFmpeg, ADR-004).
  static Backend ResolveBackend(Backend force);

  static void RegisterVideo(Backend b, VideoCreator fn);
  static void RegisterAudio(Backend b, AudioCreator fn);
  static void RegisterMuxer(Backend b, MuxerCreator fn);

  // Create an instance for the config's backend (kAuto -> platform default).
  // Returns nullptr if no matching backend is registered for the resolved
  // platform/config.
  static std::unique_ptr<VideoEncoder> CreateVideo(const VideoConfig& cfg);
  static std::unique_ptr<AudioEncoder> CreateAudio(const AudioConfig& cfg);
  static std::unique_ptr<Muxer> CreateMuxer(const MuxerConfig& cfg);

  // Create an encoder already wired for push mode: resolves the backend,
  // constructs the instance, Init()s it, and attaches `sink` as the packet
  // output (e.g. a PacketQueue, which implements PacketSink). The encoder is
  // delivered in value() on success; on failure status() carries the failing
  // step (kPlatformUnsupported when no backend is registered, otherwise the
  // Init()/SetOutputSink() status). `sink` may be nullptr to skip push-mode
  // wiring (the encoder is still Init()ed).
  static Result<std::unique_ptr<VideoEncoder>> CreateVideo(const VideoConfig& cfg,
                                                           PacketSink* sink);
  static Result<std::unique_ptr<AudioEncoder>> CreateAudio(const AudioConfig& cfg,
                                                           PacketSink* sink);

 private:
  CodecFactory() = delete;  // static-class: no instances

  struct Registry {
    std::mutex mu;
    std::unordered_map<Backend, VideoCreator> video;
    std::unordered_map<Backend, AudioCreator> audio;
    std::unordered_map<Backend, MuxerCreator> mux;
  };

  static Registry& reg();
};

}  // namespace codec
}  // namespace video
