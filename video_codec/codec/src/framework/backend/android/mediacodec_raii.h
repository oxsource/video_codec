// mediacodec_raii.h
#pragma once

#include <memory>

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>

namespace video {
namespace codec {
namespace android {

// NDK delete helpers return media_status_t (and take a single pointer, unlike
// the FFmpeg av*_free(T**) convention); wrap them so the RAII deleter can
// ignore the result. The wrapper stays in an android-only TU (this header is
// only included from the android backend's .cc files, which already compile
// against the NDK headers).

inline void MediaCodecFree(AMediaCodec* p) noexcept { AMediaCodec_delete(p); }
inline void MediaFormatFree(AMediaFormat* p) noexcept { AMediaFormat_delete(p); }
inline void MediaMuxerFree(AMediaMuxer* p) noexcept { AMediaMuxer_delete(p); }

template <typename T, void (*Free)(T*)>
struct Deleter {
  void operator()(T* p) const noexcept {
    if (p) Free(p);
  }
};

template <typename T, void (*Free)(T*)>
using Ptr = std::unique_ptr<T, Deleter<T, Free>>;

// Concrete aliases for the NDK objects the MediaCodec backends own. A
// destructor releases them even if Release() is never called.
using MediaCodecPtr = Ptr<AMediaCodec, MediaCodecFree>;
using MediaFormatPtr = Ptr<AMediaFormat, MediaFormatFree>;
using MediaMuxerPtr = Ptr<AMediaMuxer, MediaMuxerFree>;

}  // namespace android
}  // namespace codec
}  // namespace video
