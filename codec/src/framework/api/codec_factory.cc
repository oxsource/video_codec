// codec_factory.cc
#include "codec_factory.h"

#include <mutex>
#include <unordered_map>

#include "audio_encoder.h"
#include "muxer.h"
#include "video_encoder.h"

namespace video {
namespace codec {

Backend CodecFactory::ResolveBackend(Backend force) {
  if (force != Backend::kAuto) return force;
#if defined(__ANDROID__)
  return Backend::kAndroid;
#else
  // Apple currently falls back to FFmpeg; VideoToolbox is reserved (ADR-004).
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
