// codec_factory.cc
#include "codec/src/framework/api/codec_factory.h"

#include <mutex>
#include <unordered_map>

#include "codec/src/framework/api/audio_encoder.h"
#include "codec/src/framework/api/muxer.h"
#include "codec/src/framework/api/video_encoder.h"

namespace video {
namespace codec {

Backend CodecFactory::ResolveBackend(Backend force) {
  if (force != Backend::kAuto) return force;
#if defined(__ANDROID__)
  return Backend::kAndroid;
#else
  // Apple currently falls back to FFmpeg; a native Apple backend is reserved
  // (ADR-004).
  return Backend::kFFmpeg;
#endif
}

CodecFactory::Registry& CodecFactory::reg() {
  static Registry r;
  return r;
}

void CodecFactory::RegisterVideo(Backend b, VideoCreator fn) {
  std::lock_guard<std::mutex> lk(reg().mu);
  reg().video[b] = std::move(fn);
}

void CodecFactory::RegisterAudio(Backend b, AudioCreator fn) {
  std::lock_guard<std::mutex> lk(reg().mu);
  reg().audio[b] = std::move(fn);
}

std::unique_ptr<VideoEncoder> CodecFactory::CreateVideo(const VideoConfig& cfg) {
  Backend b = ResolveBackend(cfg.backend);
  std::lock_guard<std::mutex> lk(reg().mu);
  auto it = reg().video.find(b);
  if (it == reg().video.end()) return nullptr;
  return it->second(cfg);
}

std::unique_ptr<AudioEncoder> CodecFactory::CreateAudio(const AudioConfig& cfg) {
  Backend b = ResolveBackend(cfg.backend);
  std::lock_guard<std::mutex> lk(reg().mu);
  auto it = reg().audio.find(b);
  if (it == reg().audio.end()) return nullptr;
  return it->second(cfg);
}

// Push-mode convenience overloads: create + Init + SetOutputSink in one call.
// The failing step's status is propagated through the Result (an unregistered
// backend reports kPlatformUnsupported, mirroring the nullptr contract of the
// plain Create* overloads).
Result<std::unique_ptr<VideoEncoder>> CodecFactory::CreateVideo(const VideoConfig& cfg,
                                                                PacketSink* sink) {
  std::unique_ptr<VideoEncoder> encoder = CreateVideo(cfg);
  if (!encoder) return Err<std::unique_ptr<VideoEncoder>>(Status::kPlatformUnsupported);
  Status s = encoder->Init();
  if (s != Status::kOk) return Err<std::unique_ptr<VideoEncoder>>(s);
  if (sink) {
    s = encoder->SetOutputSink(sink);
    if (s != Status::kOk) return Err<std::unique_ptr<VideoEncoder>>(s);
  }
  return Ok(std::move(encoder));
}

Result<std::unique_ptr<AudioEncoder>> CodecFactory::CreateAudio(const AudioConfig& cfg,
                                                                PacketSink* sink) {
  std::unique_ptr<AudioEncoder> encoder = CreateAudio(cfg);
  if (!encoder) return Err<std::unique_ptr<AudioEncoder>>(Status::kPlatformUnsupported);
  Status s = encoder->Init();
  if (s != Status::kOk) return Err<std::unique_ptr<AudioEncoder>>(s);
  if (sink) {
    s = encoder->SetOutputSink(sink);
    if (s != Status::kOk) return Err<std::unique_ptr<AudioEncoder>>(s);
  }
  return Ok(std::move(encoder));
}

void CodecFactory::RegisterMuxer(Backend b, MuxerCreator fn) {
  std::lock_guard<std::mutex> lk(reg().mu);
  reg().mux[b] = std::move(fn);
}

std::unique_ptr<Muxer> CodecFactory::CreateMuxer(const MuxerConfig& cfg) {
  Backend b = ResolveBackend(cfg.backend);
  std::lock_guard<std::mutex> lk(reg().mu);
  auto it = reg().mux.find(b);
  if (it == reg().mux.end()) return nullptr;
  return it->second(cfg);
}

// Static factory entry points declared on the abstract classes.
std::unique_ptr<VideoEncoder> VideoEncoder::Create(const VideoConfig& c) {
  return CodecFactory::CreateVideo(c);
}

std::unique_ptr<AudioEncoder> AudioEncoder::Create(const AudioConfig& c) {
  return CodecFactory::CreateAudio(c);
}

std::unique_ptr<Muxer> Muxer::Create(const MuxerConfig& c) { return CodecFactory::CreateMuxer(c); }

}  // namespace codec
}  // namespace video
