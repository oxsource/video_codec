// queue_iface.h
#pragma once

#include "core/packet_sink.h"
#include "core/status.h"
#include "core/types.h"

namespace video {
namespace codec {

// Back-pressure policy applied when the ring is full.
//   kBlock   : Push() blocks until space is available (natural flow control).
//   kLatest  : the oldest unconsumed packet is overwritten, keeping the newest
//              (lossy, real-time).
//   kError   : Push() returns kBackendUnavailable immediately (strict
//              pipelines).
enum class Backpressure { kBlock, kLatest, kError };

// NOTE: PacketSink now lives in core/packet_sink.h — it is shared by the
// queue (producer side), consumers, and the api Muxer interface, so `api`
// may inherit it without depending on `queue`.

// Consumer endpoint. The drain loop pulls packets from here.
class PacketSource {
 public:
  virtual ~PacketSource() = default;
  // Low-level blocking pull. Blocks up to `deadline_us` (<=0 => non-blocking).
  // Returns Status::kOk on a packet, Status::kEmpty on timeout/empty, or
  // Status::kEos after MarkEos() and the source is drained.
  virtual Status Pull(VideoPacket& out, int64_t deadline_us) = 0;
  virtual Status Pull(AudioPacket& out, int64_t deadline_us) = 0;

  // Await mechanism replacing the former PacketPump: blocks on the calling
  // thread, delivering every packet (video and audio) to `sink` in order until
  // EOS, then calls sink.Finish(). A failing Push is logged, Finish() is
  // called, and Await returns kEncodeFailed (it must not swallow errors and
  // spin).
  virtual Status Await(PacketSink& sink, int64_t deadline_us = 100'000) = 0;

  virtual void MarkEos() = 0;  // encoder signals end of stream
};

}  // namespace codec
}  // namespace video
