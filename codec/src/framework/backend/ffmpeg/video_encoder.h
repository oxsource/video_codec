// video_encoder.h
#pragma once

#include <memory>

#include "api/encoder_lifecycle.h"
#include "api/video_encoder.h"
#include "backend/ffmpeg/ffmpeg_raii.h"
#include "core/types.h"

namespace video {
namespace codec {

class PacketSink;  // defined in queue/queue_iface.h (included by the .cc)

// FFmpeg (libx264 / libx265) video encoder. Software path: CPU frames only;
// Encode(NativeBuffer) returns kUnsupportedOperation (no HW surface).
class FFmpegVideoEncoder : public VideoEncoder {
 public:
  explicit FFmpegVideoEncoder(const VideoEncoderConfig& config);
  ~FFmpegVideoEncoder() override;

  Status Init() override;
  Result<VideoPacket> Encode(const VideoFrame& frame) override;
  Result<VideoPacket> Encode(const NativeBuffer& buf) override;
  Result<VideoPacket> Flush() override;
  void Release() override;
  Status SetOutputSink(PacketSink* sink) override;

 private:
  // Copy a framework VideoFrame into `frame_`, honoring stride. Returns
  // kUnsupportedFormat for pixel formats this backend cannot accept.
  Status CopyFrame(const VideoFrame& frame);

  // Pull available packets from the codec, run them through the Annex-B
  // bitstream filter, and return the resulting VideoPacket (empty if none
  // yet).
  Result<VideoPacket> Drain(bool drain_eof);

  VideoEncoderConfig config_;
  EncoderLifecycle lifecycle_;
  ffmpeg::AvCodecContextPtr ctx_;
  ffmpeg::AvFramePtr frame_;
  ffmpeg::AvBsfPtr bsf_;
  ffmpeg::AvPacketPtr pkt_;
  PacketSink* sink_ = nullptr;  // push mode; non-owning, cleared on Release()
  int64_t pts_ = 0;
};

}  // namespace codec
}  // namespace video
