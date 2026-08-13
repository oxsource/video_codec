// audio_encoder.h
#pragma once

#include <memory>

#include "api/audio_encoder.h"
#include "api/encoder_lifecycle.h"
#include "backend/ffmpeg/ffmpeg_raii.h"
#include "core/types.h"

namespace video {
namespace codec {

class OutputSink;  // defined in queue/queue_iface.h (included by the .cc)

// FFmpeg (AAC) audio encoder. Converts interleaved S16 PCM (AudioFrame) to the
// planar-float layout libavcodec's AAC encoder requires.
class FFmpegAudioEncoder : public AudioEncoder {
 public:
  explicit FFmpegAudioEncoder(const AudioEncoderConfig& config);
  ~FFmpegAudioEncoder() override;

  Status Init() override;
  Result<AudioPacket> Encode(const AudioFrame& frame) override;
  Result<AudioPacket> Flush() override;
  void Release() override;
  Status SetOutputSink(OutputSink* sink) override;

 private:
  Result<AudioPacket> Drain(bool drain_eof);

  AudioEncoderConfig config_;
  EncoderLifecycle lifecycle_;
  ffmpeg::Ptr<AVCodecContext, avcodec_free_context> ctx_;
  ffmpeg::Ptr<AVFrame, av_frame_free> frame_;
  ffmpeg::Ptr<AVPacket, av_packet_free> pkt_;
  OutputSink* sink_ = nullptr;  // push mode; non-owning, cleared on Release()
  int64_t pts_ = 0;
};

}  // namespace codec
}  // namespace video
