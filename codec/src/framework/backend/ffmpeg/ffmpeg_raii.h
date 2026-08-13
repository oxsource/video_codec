// ffmpeg_raii.h
#pragma once

#include <memory>

struct AVCodecContext;
struct AVFrame;
struct AVBSFContext;
struct AVPacket;

// FFmpeg objects are freed through their av*_free(T**) helpers, which null the
// pointer. Wrap them in unique_ptr so a destructor releases them even if the
// encoder's Release() is never called. Only the free functions are declared
// here (they have stable C linkage/ABI); the full FFmpeg headers stay in the
// .cc files.
extern "C" {
void avcodec_free_context(AVCodecContext**);
void av_frame_free(AVFrame**);
void av_bsf_free(AVBSFContext**);
void av_packet_free(AVPacket**);
}

namespace video {
namespace codec {
namespace ffmpeg {

template <typename T, void (*Free)(T**)>
struct Deleter {
  void operator()(T* p) const noexcept {
    if (p) Free(&p);
  }
};

template <typename T, void (*Free)(T**)>
using Ptr = std::unique_ptr<T, Deleter<T, Free>>;

}  // namespace ffmpeg
}  // namespace codec
}  // namespace video
