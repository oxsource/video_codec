// register.cc
// Backend self-registration. When this library is linked (selected by `public`
// via select()), its static initializer registers the FFmpeg creators so the
// factory in `api` can instantiate them without `api` depending on `backend/*`.
#include "api/codec_factory.h"
#include "backend/ffmpeg/audio_encoder.h"
#include "backend/ffmpeg/ffmpeg_muxer.h"
#include "backend/ffmpeg/video_encoder.h"

namespace video {
namespace codec {
namespace {

struct RegisterFFmpeg {
  RegisterFFmpeg() {
    CodecFactory::RegisterVideo(Backend::kFFmpeg, [](const VideoConfig& c) {
      return std::make_unique<FFmpegVideoEncoder>(c);
    });
    CodecFactory::RegisterAudio(Backend::kFFmpeg, [](const AudioConfig& c) {
      return std::make_unique<FFmpegAudioEncoder>(c);
    });
    CodecFactory::RegisterMuxer(
        Backend::kFFmpeg, [](const MuxerConfig& c) { return std::make_unique<FFmpegMuxer>(c); });
  }
};

// Force the registration to run when this library is loaded.
RegisterFFmpeg g_register_ffmpeg;

}  // namespace
}  // namespace codec
}  // namespace video
