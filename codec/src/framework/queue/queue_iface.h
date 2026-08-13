// queue_iface.h
#pragma once

#include "core/status.h"
#include "core/types.h"

namespace video {
namespace codec {

// Back-pressure policy applied when the ring is full.
//   kBlock   : Consume() blocks until space is available (natural flow
//   control). kLatest  : the oldest unconsumed packet is overwritten, keeping
//   the newest
//              (lossy, real-time).
//   kError   : Consume() returns kBackendUnavailable immediately (strict
//              pipelines).
enum class Backpressure { kBlock, kLatest, kError };

// Destination for encoded packets. One contract serves BOTH transport
// directions:
//   - producer -> queue : the encoder hands packets to the queue through
//     Consume(); PacketQueue implements PacketSink for this side.
//   - queue -> consumer : PacketSource::Await() delivers packets to the
//     consumer through the same Consume(); consumer::PacketConsumer
//     implements PacketSink for this side.
// Flush() marks a segment boundary (producer flush); Finish() is EOS teardown
// (consumer finish).
class PacketSink {
 public:
  virtual ~PacketSink() = default;
  virtual Status Consume(VideoPacket&& pkt) = 0;
  virtual Status Consume(AudioPacket&& pkt) = 0;
  virtual Status Flush() { return Status::kOk; }   // segment boundary
  virtual Status Finish() { return Status::kOk; }  // EOS / teardown
};

// Consumer endpoint. The drain loop awaits packets from here.
class PacketSource {
 public:
  virtual ~PacketSource() = default;
  // Low-level blocking next. Blocks up to `deadline_us` (<=0 => non-blocking).
  // Returns Status::kOk on a packet, Status::kEmpty on timeout/empty, or
  // Status::kEos after MarkEos() and the source is drained.
  virtual Status Next(VideoPacket& out, int64_t deadline_us) = 0;
  virtual Status Next(AudioPacket& out, int64_t deadline_us) = 0;

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
