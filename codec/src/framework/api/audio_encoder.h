// audio_encoder.h
#pragma once

#include <memory>

#include "core/result.h"
#include "core/types.h"

namespace video {
namespace codec {

// Abstract audio encoder (backend subclass). No Surface/InputSurface for audio.
class AudioEncoder {
 public:
  virtual ~AudioEncoder() = default;

  static std::unique_ptr<AudioEncoder> Create(const AudioEncoderConfig& config);

  virtual StatusCode Init() = 0;
  virtual Result<AudioPacket> Encode(const AudioFrame& frame) = 0;
  virtual Result<AudioPacket> Flush() = 0;
  virtual void Release() = 0;
};

}  // namespace codec
}  // namespace video
