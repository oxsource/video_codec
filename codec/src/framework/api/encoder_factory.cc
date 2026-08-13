// encoder_factory.cc
#include "api/encoder_factory.h"

#include <mutex>
#include <unordered_map>

#include "api/audio_encoder.h"
#include "api/muxer.h"
#include "api/video_encoder.h"

namespace video {
namespace codec {

Backend ResolveBackend(Backend force) {
  if (force != Backend::kAuto) return force;
#if defined(__ANDROID__)
  return Backend::kAndroid;
#else
  // Apple currently falls back to FFmpeg; VideoToolbox is reserved (ADR-004).
  return Backend::kFFmpeg;
#endif
}

namespace {

struct Registry {
  std::mutex mu;
  std::unordered_map<Backend, VideoEncoderCreator> video;
  std::unordered_map<Backend, AudioEncoderCreator> audio;
  std::unordered_map<Backend, MuxerCreator> mux;
};

Registry& reg() {
  static Registry r;
  return r;
}

}  // namespace

void RegisterVideoEncoder(Backend b, VideoEncoderCreator fn) {
  std::lock_guard<std::mutex> lk(reg().mu);
  reg().video[b] = std::move(fn);
}

void RegisterAudioEncoder(Backend b, AudioEncoderCreator fn) {
  std::lock_guard<std::mutex> lk(reg().mu);
  reg().audio[b] = std::move(fn);
}

std::unique_ptr<VideoEncoder> CreateVideoEncoder(
    const VideoEncoderConfig& cfg) {
  Backend b = ResolveBackend(cfg.backend);
  std::lock_guard<std::mutex> lk(reg().mu);
  auto it = reg().video.find(b);
  if (it == reg().video.end()) return nullptr;
  return it->second(cfg);
}

std::unique_ptr<AudioEncoder> CreateAudioEncoder(
    const AudioEncoderConfig& cfg) {
  Backend b = ResolveBackend(cfg.backend);
  std::lock_guard<std::mutex> lk(reg().mu);
  auto it = reg().audio.find(b);
  if (it == reg().audio.end()) return nullptr;
  return it->second(cfg);
}

void RegisterMuxer(Backend b, MuxerCreator fn) {
  std::lock_guard<std::mutex> lk(reg().mu);
  reg().mux[b] = std::move(fn);
}

std::unique_ptr<Muxer> CreateMuxer(const MuxerConfig& cfg) {
  Backend b = ResolveBackend(cfg.backend);
  std::lock_guard<std::mutex> lk(reg().mu);
  auto it = reg().mux.find(b);
  if (it == reg().mux.end()) return nullptr;
  return it->second(cfg);
}

// Static factory entry points declared on the abstract classes.
std::unique_ptr<VideoEncoder> VideoEncoder::Create(
    const VideoEncoderConfig& c) {
  return CreateVideoEncoder(c);
}

std::unique_ptr<AudioEncoder> AudioEncoder::Create(
    const AudioEncoderConfig& c) {
  return CreateAudioEncoder(c);
}

std::unique_ptr<Muxer> Muxer::Create(const MuxerConfig& c) {
  return CreateMuxer(c);
}

}  // namespace codec
}  // namespace video
