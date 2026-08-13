// queue_iface.h
#pragma once

#include "core/status.h"
#include "core/types.h"

namespace video {
namespace codec {

// Back-pressure policy applied when the ring is full.
//   kBlock      : Submit() blocks until space is available (natural flow
//   control). kDropOldest : the oldest unconsumed packet is overwritten (lossy,
//   real-time). kError      : Submit() returns kBackendUnavailable immediately
//   (strict
//                 pipelines).
enum class Backpressure { kBlock, kDropOldest, kError };

// Result of a consumer Pop().
enum class PopResult { kOk, kEmpty, kEos };

// Producer endpoint. The encoder (or any producer) pushes encoded packets here.
// Video and audio share one Submit — the packet's PacketType distinguishes
// them.
class OutputSink {
 public:
  virtual ~OutputSink() = default;
  virtual StatusCode Submit(Packet&& pkt) = 0;
  virtual StatusCode Flush() { return StatusCode::kOk; }
};

// Consumer endpoint. The drain loop (PacketPump) pops from here.
class PacketSource {
 public:
  virtual ~PacketSource() = default;
  // Blocks up to `deadline_us` (<=0 => non-blocking). Returns kOk on a packet,
  // kEmpty on timeout/empty, kEos after MarkEos() and the queue is drained.
  virtual PopResult Pop(Packet& out, int64_t deadline_us) = 0;
  virtual void MarkEos() = 0;  // encoder signals end of stream
};

}  // namespace codec
}  // namespace video
