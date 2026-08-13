// packet_sink.h
#pragma once

#include <memory>

#include "core/status.h"
#include "core/types.h"

namespace video {
namespace codec {

// Destination for encoded packets. One contract serves BOTH transport
// directions:
//   - producer -> queue : the encoder hands packets to the queue through
//     Push(); PacketQueue implements PacketSink for this side.
//   - queue -> consumer/muxer : PacketSource::Await() delivers packets to the
//     consumer through the same Push(); consumer::PacketConsumer and the api
//     Muxer interface both implement PacketSink for this side.
// Flush() marks a segment boundary (producer flush); Finish() is EOS teardown
// (consumer finish).
class PacketSink {
 public:
  virtual ~PacketSink() = default;
  virtual Status Push(VideoPacket&& pkt) = 0;
  virtual Status Push(AudioPacket&& pkt) = 0;
  virtual Status Flush() { return Status::kOk; }   // segment boundary
  virtual Status Finish() { return Status::kOk; }  // EOS / teardown

  // Upcast a derived `unique_ptr<T>` (T : PacketSink) to a base PacketSink*.
  // Lets callers select between sinks of different concrete types through the
  // common interface (e.g. a conditional `a ? Ptr(x) : Ptr(y)`).
  template <typename T>
  static PacketSink* Ptr(const std::unique_ptr<T>& p) {
    return p.get();
  }
};

}  // namespace codec
}  // namespace video
