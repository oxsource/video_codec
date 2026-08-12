// encoder_factory.h
#pragma once

#include <functional>
#include <memory>

#include "core/types.h"

namespace video {
namespace codec {

class VideoEncoder;
class AudioEncoder;

// Resolve which backend to instantiate given a force_backend request and the
// current platform. kAuto -> platform select (non-Android -> FFmpeg; Android ->
// MediaCodec; Apple falls back to FFmpeg, ADR-004).
Backend ResolveBackend(Backend force);

// Backends self-register here so `api` never depends on `backend/*`. The
// selected backend (wired by `public` via select()) runs its registration at
// static init.
using VideoEncoderCreator =
    std::function<std::unique_ptr<VideoEncoder>(const VideoEncoderConfig&)>;
void RegisterVideoEncoder(Backend b, VideoEncoderCreator fn);

using AudioEncoderCreator =
    std::function<std::unique_ptr<AudioEncoder>(const AudioEncoderConfig&)>;
void RegisterAudioEncoder(Backend b, AudioEncoderCreator fn);

std::unique_ptr<VideoEncoder> CreateVideoEncoder(const VideoEncoderConfig& cfg);
std::unique_ptr<AudioEncoder> CreateAudioEncoder(const AudioEncoderConfig& cfg);

}  // namespace codec
}  // namespace video
