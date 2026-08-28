// mediacodec_video.h
#pragma once

#include <cstdint>
#include <vector>

#include <android/native_window.h>

#include "src/framework/api/encoder_lifecycle.h"
#include "src/framework/backend/android/mediacodec_raii.h"
#include "src/framework/api/video_encoder.h"
#include "src/framework/core/types.h"

namespace video {
namespace codec {

class PacketSink;

// Android MediaCodec (NDK) video encoder. Two input modes (mutually exclusive,
// declared via VideoConfig.input_surface):
//   CPU mode (default): framework VideoFrames are copied into MediaCodec input
//     buffers; output is assembled into Annex-B (SPS/PPS captured from the
//     CODEC_CONFIG buffer and prepended to keyframes).
//   Surface mode (input_surface=true): the codec is configured with
//     COLOR_FormatSurface and a hardware input surface is created via
//     AMediaCodec_createInputSurface; CreateInputSurface() returns the
//     ANativeWindow* handle (void*) for the caller to draw into. Encode() is
//     rejected in this mode (input-mode contract C-012).
class MediaCodecVideoEncoder : public VideoEncoder {
 public:
  explicit MediaCodecVideoEncoder(const VideoConfig& config);
  ~MediaCodecVideoEncoder() override;

  Status Init() override;
  Result<VideoPacket> Encode(const VideoFrame& frame) override;
  Result<VideoPacket> Encode(const NativeBuffer& buf) override;
  void* CreateInputSurface() override;
  Status Poll() override;
  Result<VideoPacket> Flush() override;
  void Release() override;
  Status SetOutputSink(PacketSink* sink) override;

 private:
  // Copy one frame's pixels into a MediaCodec input buffer (color format from
  // config_.input_format) and queue it. Returns kOk on success.
  Status QueueInput(const VideoFrame& frame);

  // Pull every available output buffer, assemble Annex-B access units, and
  // deliver them (push mode -> sink, pull mode -> return the last one).
  // `drain_eof` stops after the END_OF_STREAM marker; `deadline_us > 0` bounds
  // the wait when EOS is not guaranteed (surface path, research R3).
  Result<VideoPacket> Drain(bool drain_eof, int64_t deadline_us = 0);

  // True when the encoder is in surface-input mode (config_.input_surface).
  bool SurfaceMode() const { return config_.input_surface; }

  VideoConfig config_;
  EncoderLifecycle lifecycle_;
  android::MediaCodecPtr codec_;   // RAII: AMediaCodec_delete on destruction
  android::MediaFormatPtr format_;  // RAII: AMediaFormat_delete on destruction
  PacketSink* sink_ = nullptr;  // push mode; non-owning, cleared on Release()
  ANativeWindow* surface_window_ = nullptr;  // hardware input surface (surface mode)
  int64_t pts_ = 0;             // input presentation clock (microseconds)
  std::vector<uint8_t> sps_;    // from the CODEC_CONFIG buffer
  std::vector<uint8_t> pps_;
};

}  // namespace codec
}  // namespace video
