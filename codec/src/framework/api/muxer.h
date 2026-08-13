// muxer.h
#pragma once

#include <memory>

#include "core/packet_sink.h"
#include "core/result.h"
#include "core/types.h"

namespace video {
namespace codec {

class ByteSink;  // fwd-declared: `api` stays free of an `io` dependency.

// Generic muxer; every backend subclasses this. It implements PacketSink so a
// PacketSource::Await() can hand packets straight to it (queue -> muxer,
// no adapter) — peer of VideoEncoder/AudioEncoder, reference Android
// MediaCodec where encoding and muxing are independent layers.
//
// The container opens lazily on the first keyframe (it needs SPS/PPS for the
// avcC extradata); earlier non-keyframes are dropped. Not thread-safe: one
// instance per thread.
class Muxer : public PacketSink {
 public:
  ~Muxer() override = default;

  // Resolve backend + construct. Returns nullptr if no matching backend is
  // linked for this platform/config.
  static std::unique_ptr<Muxer> Create(const MuxerConfig& config);

  // Attach the byte output target (non-owning; must outlive this muxer).
  // MUST be called before the first Push(); nullptr detaches.
  virtual Status SetOutput(ByteSink* sink) = 0;

  Status Push(VideoPacket&& pkt) override = 0;
  Status Push(AudioPacket&& pkt) override {
    return Status::kUnsupportedOperation;  // v1: video-only
  }
  Status Flush() override = 0;   // flush buffered fragment bytes to the sink
  Status Finish() override = 0;  // write trailer + final commit to the sink

  virtual void Release() = 0;  // free external resources (idempotent)
};

}  // namespace codec
}  // namespace video
