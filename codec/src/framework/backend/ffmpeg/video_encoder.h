// video_encoder.h
#pragma once

#include <memory>

#include "api/encoder_lifecycle.h"
#include "api/video_encoder.h"
#include "core/types.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct AVBSFContext;

namespace video {
namespace codec {

class OutputSink;  // defined in queue/queue_iface.h (included by the .cc)

// FFmpeg (libx264 / libx265) video encoder. Software path: CPU frames only;
// Encode(NativeBuffer) returns kUnsupportedOperation (no HW surface).
class FFmpegVideoEncoder : public VideoEncoder {
 public:
  explicit FFmpegVideoEncoder(const VideoEncoderConfig& config);
  ~FFmpegVideoEncoder() override;

  StatusCode Init() override;
  Result<EncodedPacket> Encode(const VideoFrame& frame) override;
  Result<EncodedPacket> Encode(const NativeBuffer& buf) override;
  Result<EncodedPacket> Flush() override;
  void Release() override;
  StatusCode SetOutputSink(OutputSink* sink) override;

 private:
  // Copy a framework VideoFrame into `frame_`, honoring stride. Returns
  // kUnsupportedFormat for pixel formats this backend cannot accept.
  StatusCode CopyFrame(const VideoFrame& frame);

  // Pull available packets from the codec, run them through the Annex-B
  // bitstream filter, and return the resulting EncodedPacket (empty if none
  // yet).
  Result<EncodedPacket> Drain(bool drain_eof);

  VideoEncoderConfig config_;
  EncoderLifecycle lifecycle_;
  AVCodecContext* ctx_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVBSFContext* bsf_ = nullptr;
  AVPacket* pkt_ = nullptr;
  OutputSink* sink_ = nullptr;  // push mode; non-owning, cleared on Release()
  int64_t pts_ = 0;
  bool initialized_ = false;
};

}  // namespace codec
}  // namespace video
