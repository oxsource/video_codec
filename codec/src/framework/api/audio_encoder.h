// audio_encoder.h
#pragma once

#include <memory>

#include "core/result.h"
#include "core/types.h"

namespace video {
namespace codec {

class PacketSink;  // fwd-declared: `api` stays free of a `queue` dependency.

// Abstract audio encoder (backend subclass). No Surface/InputSurface for audio.
class AudioEncoder {
 public:
  virtual ~AudioEncoder() = default;

  static std::unique_ptr<AudioEncoder> Create(const AudioConfig& config);

  virtual Status Init() = 0;
  virtual Result<AudioPacket> Encode(const AudioFrame& frame) = 0;
  virtual Result<AudioPacket> Flush() = 0;
  virtual void Release() = 0;

  // Attach a packet sink to enable push mode (see
  // VideoEncoder::SetOutputSink). Audio packets are handed to the sink via
  // PacketSink::Push(AudioPacket&&).
  virtual Status SetOutputSink(PacketSink* sink) {
    (void)sink;
    return Status::kUnsupportedOperation;
  }
};

}  // namespace codec
}  // namespace video
