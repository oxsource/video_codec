// codec_factory.h
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "core/types.h"

namespace video {
namespace codec {

class VideoEncoder;
class AudioEncoder;
class Muxer;

// Backend creator functions self-registered by each backend.
using VideoEncoderCreator = std::function<std::unique_ptr<VideoEncoder>(const VideoEncoderConfig&)>;
using AudioEncoderCreator = std::function<std::unique_ptr<AudioEncoder>(const AudioEncoderConfig&)>;
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

  static void RegisterVideoEncoder(Backend b, VideoEncoderCreator fn);
  static void RegisterAudioEncoder(Backend b, AudioEncoderCreator fn);
  static void RegisterMuxer(Backend b, MuxerCreator fn);

  // Create an instance for the config's backend (kAuto -> platform default).
  // Returns nullptr if no matching backend is registered for the resolved
  // platform/config.
  static std::unique_ptr<VideoEncoder> CreateVideoEncoder(const VideoEncoderConfig& cfg);
  static std::unique_ptr<AudioEncoder> CreateAudioEncoder(const AudioEncoderConfig& cfg);
  static std::unique_ptr<Muxer> CreateMuxer(const MuxerConfig& cfg);

 private:
  CodecFactory() = delete;  // static-class: no instances

  struct Registry {
    std::mutex mu;
    std::unordered_map<Backend, VideoEncoderCreator> video;
    std::unordered_map<Backend, AudioEncoderCreator> audio;
    std::unordered_map<Backend, MuxerCreator> mux;
  };

  static Registry& reg();
};

}  // namespace codec
}  // namespace video
