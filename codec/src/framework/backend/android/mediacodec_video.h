// mediacodec_video.h
#pragma once

#include <cstdint>
#include <vector>

#include "encoder_lifecycle.h"
#include "mediacodec_raii.h"
#include "video_encoder.h"
#include "types.h"

namespace video {
namespace codec {

class PacketSink;

// Android MediaCodec (NDK) video encoder. v1 = CPU input path only: framework
// VideoFrames are copied into MediaCodec input buffers; output is assembled
// into Annex-B (SPS/PPS captured from the CODEC_CONFIG buffer and prepended to
// keyframes). Encode(NativeBuffer) and CreateInputSurface() are unsupported in
// v1 (spec FR-004 / C-012), mirroring the FFmpeg backend's software-only path.
class MediaCodecVideoEncoder : public VideoEncoder {
 public:
  explicit MediaCodecVideoEncoder(const VideoConfig& config);
  ~MediaCodecVideoEncoder() override;

  Status Init() override;
  Result<VideoPacket> Encode(const VideoFrame& frame) override;
  Result<VideoPacket> Encode(const NativeBuffer& buf) override;
  Result<VideoPacket> Flush() override;
  void Release() override;
  Status SetOutputSink(PacketSink* sink) override;

 private:
  // Copy one frame's pixels into a MediaCodec input buffer (color format from
  // config_.input_format) and queue it. Returns kOk on success.
  Status QueueInput(const VideoFrame& frame);

  // Pull every available output buffer, assemble Annex-B access units, and
  // deliver them (push mode -> sink, pull mode -> return the last one).
  // `drain_eof` stops after the END_OF_STREAM marker.
  Result<VideoPacket> Drain(bool drain_eof);

  VideoConfig config_;
  EncoderLifecycle lifecycle_;
  android::MediaCodecPtr codec_;   // RAII: AMediaCodec_delete on destruction
  android::MediaFormatPtr format_;  // RAII: AMediaFormat_delete on destruction
  PacketSink* sink_ = nullptr;  // push mode; non-owning, cleared on Release()
  int64_t pts_ = 0;             // input presentation clock (microseconds)
  std::vector<uint8_t> sps_;    // from the CODEC_CONFIG buffer
  std::vector<uint8_t> pps_;
};

}  // namespace codec
}  // namespace video
