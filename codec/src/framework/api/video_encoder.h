// video_encoder.h
#pragma once

#include <memory>

#include "core/result.h"
#include "core/types.h"

namespace video {
namespace codec {

class InputSurface;
class OutputSink;  // fwd-declared: `api` stays free of a `queue` dependency.

// Abstract video encoder. Every backend subclasses this; the contract is frozen
// by contracts/encoder-contract.md. Not thread-safe: one instance per thread.
class VideoEncoder {
 public:
  virtual ~VideoEncoder() = default;

  // Resolve backend + allocate, but do NOT open the external encoder yet.
  // Returns nullptr if no matching backend is linked for this platform/config.
  static std::unique_ptr<VideoEncoder> Create(const VideoEncoderConfig& config);

  virtual Status Init() = 0;  // -> Initialized

  // CPU path. Init() must have succeeded; returns kNotInitialized otherwise.
  virtual Result<VideoPacket> Encode(const VideoFrame& frame) = 0;

  // Zero-copy path. Returns kUnsupportedOperation if the backend cannot consume
  // the handle type.
  virtual Result<VideoPacket> Encode(const NativeBuffer& buf) = 0;

  // Returns a drawable surface, or nullptr if the backend has no Surface
  // support (FFmpeg software path returns nullptr).
  virtual std::unique_ptr<InputSurface> CreateInputSurface() { return nullptr; }

  virtual Result<VideoPacket>
  Flush() = 0;                 // -> Flushed (drain + emit final pkt)
  virtual void Release() = 0;  // free external resources -> Released

  // Attach an output sink to enable push mode: every produced packet is handed
  // to the sink instead of returned (single destination). Pass nullptr to
  // detach (back to pull mode). Backends without push support return
  // Status::kUnsupportedOperation and stay in pull mode.
  virtual Status SetOutputSink(OutputSink* sink) {
    (void)sink;
    return Status::kUnsupportedOperation;
  }
};

}  // namespace codec
}  // namespace video
