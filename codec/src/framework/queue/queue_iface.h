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
// Video and audio are distinct types, each with its own Submit.
class OutputSink {
 public:
  virtual ~OutputSink() = default;
  virtual Status Submit(VideoPacket&& pkt) = 0;
  virtual Status Submit(AudioPacket&& pkt) = 0;
  virtual Status Flush() { return Status::kOk; }
};

// Destination for packets popped from a source. Defined in `queue` so the
// source (and its Await loop) depends only on this contract, never on the
// `consumer` module; `consumer::PacketConsumer` implements it.
class PacketSink {
 public:
  virtual ~PacketSink() = default;
  virtual Status Consume(VideoPacket&& pkt) = 0;
  virtual Status Consume(AudioPacket&& pkt) = 0;
  virtual Status Finish() { return Status::kOk; }  // EOS / teardown
};

// Consumer endpoint. The drain loop awaits packets from here.
class PacketSource {
 public:
  virtual ~PacketSource() = default;
  // Low-level blocking pop. Blocks up to `deadline_us` (<=0 => non-blocking).
  // Returns Status::kOk on a packet, Status::kEmpty on timeout/empty, or
  // Status::kEos after MarkEos() and the source is drained.
  virtual Status Pop(VideoPacket& out, int64_t deadline_us) = 0;
  virtual Status Pop(AudioPacket& out, int64_t deadline_us) = 0;

  // Await mechanism replacing the former PacketPump: blocks on the calling
  // thread, delivering every packet (video and audio) to `sink` in order until
  // EOS, then calls sink.Finish(). A failing Consume is logged, Finish() is
  // called, and Await returns kEncodeFailed (it must not swallow errors and
  // spin).
  virtual Status Await(PacketSink& sink, int64_t deadline_us = 100'000) = 0;

  virtual void MarkEos() = 0;  // encoder signals end of stream
};

}  // namespace codec
}  // namespace video
