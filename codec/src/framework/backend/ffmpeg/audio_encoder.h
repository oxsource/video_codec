// audio_encoder.h
#pragma once

#include <memory>

#include "api/audio_encoder.h"
#include "api/encoder_lifecycle.h"
#include "core/types.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace video {
namespace codec {

class OutputSink;  // defined in queue/queue_iface.h (included by the .cc)

// FFmpeg (AAC) audio encoder. Converts interleaved S16 PCM (AudioFrame) to the
// planar-float layout libavcodec's AAC encoder requires.
class FFmpegAudioEncoder : public AudioEncoder {
 public:
  explicit FFmpegAudioEncoder(const AudioEncoderConfig& config);
  ~FFmpegAudioEncoder() override;

  StatusCode Init() override;
  Result<Packet> Encode(const AudioFrame& frame) override;
  Result<Packet> Flush() override;
  void Release() override;
  StatusCode SetOutputSink(OutputSink* sink) override;

 private:
  Result<Packet> Drain(bool drain_eof);

  AudioEncoderConfig config_;
  EncoderLifecycle lifecycle_;
  AVCodecContext* ctx_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* pkt_ = nullptr;
  OutputSink* sink_ = nullptr;  // push mode; non-owning, cleared on Release()
  int64_t pts_ = 0;
};

}  // namespace codec
}  // namespace video
