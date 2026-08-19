// ffmpeg_audio.h
#pragma once

#include <memory>

#include "src/framework/api/audio_encoder.h"
#include "src/framework/api/encoder_lifecycle.h"
#include "src/framework/backend/ffmpeg/ffmpeg_raii.h"
#include "src/framework/core/types.h"

namespace video {
namespace codec {

class PacketSink;  // defined in queue/queue_iface.h (included by the .cc)

// FFmpeg (AAC) audio encoder. Converts interleaved S16 PCM (AudioFrame) to the
// planar-float layout libavcodec's AAC encoder requires.
class FFmpegAudioEncoder : public AudioEncoder {
 public:
  explicit FFmpegAudioEncoder(const AudioConfig& config);
  ~FFmpegAudioEncoder() override;

  Status Init() override;
  Result<AudioPacket> Encode(const AudioFrame& frame) override;
  Result<AudioPacket> Flush() override;
  void Release() override;
  Status SetOutputSink(PacketSink* sink) override;

 private:
  Result<AudioPacket> Drain(bool drain_eof);

  AudioConfig config_;
  EncoderLifecycle lifecycle_;
  ffmpeg::AvCodecContextPtr ctx_;
  ffmpeg::AvFramePtr frame_;
  ffmpeg::AvPacketPtr pkt_;
  PacketSink* sink_ = nullptr;  // push mode; non-owning, cleared on Release()
  int64_t pts_ = 0;
};

}  // namespace codec
}  // namespace video
