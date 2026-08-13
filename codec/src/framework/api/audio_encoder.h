// audio_encoder.h
#pragma once

#include <memory>

#include "core/result.h"
#include "core/types.h"

namespace video {
namespace codec {

class OutputSink;  // fwd-declared: `api` stays free of a `queue` dependency.

// Abstract audio encoder (backend subclass). No Surface/InputSurface for audio.
class AudioEncoder {
 public:
  virtual ~AudioEncoder() = default;

  static std::unique_ptr<AudioEncoder> Create(const AudioEncoderConfig& config);

  virtual Status Init() = 0;
  virtual Result<Packet> Encode(const AudioFrame& frame) = 0;
  virtual Result<Packet> Flush() = 0;
  virtual void Release() = 0;

  // Attach an output sink to enable push mode (see
  // VideoEncoder::SetOutputSink). Audio packets (PacketType::kAudio) are
  // handed to the sink via OutputSink::Submit(Packet&&).
  virtual Status SetOutputSink(OutputSink* sink) {
    (void)sink;
    return Status::kUnsupportedOperation;
  }
};

}  // namespace codec
}  // namespace video
