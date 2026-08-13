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

// Producer endpoint. The encoder (or any producer) pushes encoded packets here.
// Video and audio share one Submit — the packet's PacketType distinguishes
// them.
class OutputSink {
 public:
  virtual ~OutputSink() = default;
  virtual Status Submit(Packet&& pkt) = 0;
  virtual Status Flush() { return Status::kOk; }
};

// Destination for packets popped from a source. Defined in `queue` so the
// source (and its Await loop) depends only on this contract, never on the
// `consumer` module; `consumer::PacketConsumer` implements it.
class PacketSink {
 public:
  virtual ~PacketSink() = default;
  virtual Status Consume(Packet&& pkt) = 0;
  virtual Status Finish() { return Status::kOk; }  // EOS / teardown
};

// Consumer endpoint. The drain loop awaits packets from here.
class PacketSource {
 public:
  // Result of Pop(): kOk (packet in `out`), kEmpty (timeout/empty), or kEos
  // (end-of-stream and drained).
  enum class PopResult { kOk, kEmpty, kEos };

  virtual ~PacketSource() = default;
  // Low-level blocking pop. Blocks up to `deadline_us` (<=0 => non-blocking).
  // Returns kOk on a packet, kEmpty on timeout/empty, kEos after MarkEos() and
  // the source is drained.
  virtual PopResult Pop(Packet& out, int64_t deadline_us) = 0;

  // Await mechanism replacing the former PacketPump: blocks on the calling
  // thread, delivering every packet to `sink` in order until EOS, then calls
  // sink.Finish(). A failing Consume is logged, Finish() is called, and Await
  // returns kEncodeFailed (it must not swallow errors and spin).
  virtual Status Await(PacketSink& sink, int64_t deadline_us = 100'000) = 0;

  virtual void MarkEos() = 0;  // encoder signals end of stream
};

}  // namespace codec
}  // namespace video
