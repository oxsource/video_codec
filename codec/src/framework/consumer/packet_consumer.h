// packet_consumer.h
#pragma once

#include "packet_sink.h"
#include "status.h"
#include "types.h"

namespace video {
namespace codec {

// Transport-agnostic consumer of encoded packets. Both FileConsumer and
// StreamConsumer implement this; swapping one for the other is a one-line
// change and the encoder never knows which is attached.
//
// Implements core::PacketSink — the dispatch target the source's Await loop
// delivers to (Push) and the producer feeds (via the encoder's sink). A
// PacketConsumer can be handed straight to PacketSource::Await(PacketSink&).
class PacketConsumer : public PacketSink {
 public:
  ~PacketConsumer() override = default;

  // Video and audio arrive as distinct types with their own Push overloads;
  // a consumer that only handles one media returns kUnsupportedOperation for
  // the other.
  Status Push(VideoPacket&& pkt) override = 0;
  Status Push(AudioPacket&& pkt) override = 0;

  Status Flush() override { return Status::kOk; }   // segment boundary
  Status Finish() override { return Status::kOk; }  // EOS / teardown
};

}  // namespace codec
}  // namespace video
