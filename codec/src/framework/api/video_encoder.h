// video_encoder.h
#pragma once

#include <memory>

#include "export.h"
#include "result.h"
#include "types.h"

namespace video {
namespace codec {

class PacketSink;  // fwd-declared: `api` stays free of a `queue` dependency.

// Abstract video encoder. Every backend subclasses this; the contract is frozen
// by contracts/encoder-contract.md. Not thread-safe: one instance per thread.
class VIDEO_CODEC_API VideoEncoder {
 public:
  virtual ~VideoEncoder() = default;

  // Resolve backend + allocate, but do NOT open the external encoder yet.
  // Returns nullptr if no matching backend is linked for this platform/config.
  static std::unique_ptr<VideoEncoder> Create(const VideoConfig& config);

  virtual Status Init() = 0;  // -> Initialized

  // CPU path. Init() must have succeeded; returns kNotInitialized otherwise.
  virtual Result<VideoPacket> Encode(const VideoFrame& frame) = 0;

  // Zero-copy path. Returns kUnsupportedOperation if the backend cannot consume
  // the handle type.
  virtual Result<VideoPacket> Encode(const NativeBuffer& buf) = 0;

  // Returns a drawable input-surface handle, or nullptr if the backend has no
  // surface support (non-Android backends return nullptr; on Android it is the
  // ANativeWindow* from MediaCodec createInputSurface, zero-copy). The caller
  // draws into the handle; the system delivers buffers to the encoder. The
  // handle stays valid until Release(). Surface mode is declared via
  // VideoConfig.input_surface and is mutually exclusive with Encode(VideoFrame).
  virtual void* CreateInputSurface() { return nullptr; }

  // Surface-mode pump: drains any ready encoded output (delivering it per the
  // current push/pull mode) so the encoder keeps consuming input-surface
  // frames — hardware encoders stall when their output queue fills, which
  // back-pressures the input surface. The caller should invoke this after each
  // drawn frame. No-op for non-surface modes.
  virtual Status Poll() { return Status::kOk; }

  virtual Result<VideoPacket> Flush() = 0;  // -> Flushed (drain + emit final pkt)
  virtual void Release() = 0;               // free external resources -> Released

  // Attach a packet sink to enable push mode: every produced packet is handed
  // to the sink (Push) instead of returned (single destination). Pass
  // nullptr to detach (back to pull mode). Backends without push support
  // return Status::kUnsupportedOperation and stay in pull mode.
  virtual Status SetOutputSink(PacketSink* sink) {
    (void)sink;
    return Status::kUnsupportedOperation;
  }
};

}  // namespace codec
}  // namespace video
