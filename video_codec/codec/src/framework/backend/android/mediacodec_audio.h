// mediacodec_audio.h
#pragma once

#include <cstdint>
#include <vector>

#include "src/framework/api/audio_encoder.h"
#include "src/framework/api/encoder_lifecycle.h"
#include "src/framework/backend/android/mediacodec_raii.h"
#include "src/framework/core/types.h"

namespace video {
namespace codec {

class PacketSink;

// Android MediaCodec (NDK) audio encoder. v1 = AAC (`audio/mp4a-latm`) with
// S16 interleaved PCM input; output is raw AAC access units (the CODEC_CONFIG
// buffer — the AudioSpecificConfig — is captured but not emitted as a packet,
// mirroring the FFmpeg audio encoder's raw-frame output). push/pull dual mode
// via SetOutputSink, EncoderLifecycle, and EOS-flush like the video encoder.
class MediaCodecAudioEncoder : public AudioEncoder {
 public:
  explicit MediaCodecAudioEncoder(const AudioConfig& config);
  ~MediaCodecAudioEncoder() override;

  Status Init() override;
  Result<AudioPacket> Encode(const AudioFrame& frame) override;
  Result<AudioPacket> Flush() override;
  void Release() override;
  Status SetOutputSink(PacketSink* sink) override;

 private:
  // Copy one S16 interleaved PCM frame into a MediaCodec input buffer and
  // queue it. Returns kOk on success.
  Status QueueInput(const AudioFrame& frame);

  // Pull every available output buffer and deliver AAC packets (push -> sink,
  // pull -> return the last one). `drain_eof` stops after END_OF_STREAM.
  Result<AudioPacket> Drain(bool drain_eof);

  AudioConfig config_;
  EncoderLifecycle lifecycle_;
  android::MediaCodecPtr codec_;   // RAII: AMediaCodec_delete on destruction
  android::MediaFormatPtr format_;  // RAII: AMediaFormat_delete on destruction
  PacketSink* sink_ = nullptr;  // push mode; non-owning, cleared on Release()
  int64_t pts_ = 0;             // fallback presentation clock (microseconds)
  std::vector<uint8_t> asc_;    // AudioSpecificConfig from CODEC_CONFIG
  bool codec_config_sent_ = false;  // first emitted packet carries asc_
};

}  // namespace codec
}  // namespace video
