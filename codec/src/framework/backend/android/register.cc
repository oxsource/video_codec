// register.cc
//
// Static self-registration of the Android MediaCodec backend (spec 006,
// C-001). Mirrors the FFmpeg backend: a static initializer registers the
// creators into CodecFactory so `api` never depends on `backend/*`. This
// target carries `alwayslink = True` so the initializer is force-linked into
// any binary that pulls the Android backend (otherwise `Create*` returns
// nullptr).
#include "codec_factory.h"
#include "mediacodec_audio.h"
#include "mediacodec_muxer.h"
#include "mediacodec_video.h"

namespace video {
namespace codec {
namespace {

struct RegisterAndroid {
  RegisterAndroid() {
    CodecFactory::RegisterVideo(Backend::kAndroid, [](const VideoConfig& c) {
      return std::make_unique<MediaCodecVideoEncoder>(c);
    });
    CodecFactory::RegisterAudio(Backend::kAndroid, [](const AudioConfig& c) {
      return std::make_unique<MediaCodecAudioEncoder>(c);
    });
    CodecFactory::RegisterMuxer(Backend::kAndroid, [](const MuxerConfig& c) {
      return std::make_unique<MediaCodecMuxer>(c);
    });
  }
};
RegisterAndroid g_register_android;

}  // namespace
}  // namespace codec
}  // namespace video
