// packet_consumer.h
#pragma once

#include "core/status.h"
#include "core/types.h"
#include "queue/queue_iface.h"

namespace video {
namespace codec {

// Transport-agnostic consumer of encoded packets. Both FileSinkConsumer and
// StreamConsumer implement this; swapping one for the other is a one-line
// change and the encoder never knows which is attached.
//
// Inherits queue::PacketSink — the dispatch target the source's Await loop
// delivers to — and adds Flush() (segment boundary). A PacketConsumer can be
// handed straight to PacketSource::Await(PacketSink&).
class PacketConsumer : public PacketSink {
 public:
  ~PacketConsumer() override = default;

  // Video and audio arrive as distinct types with their own Consume overloads;
  // a consumer that only handles one media returns kUnsupportedOperation for
  // the other.
  Status Consume(VideoPacket&& pkt) override = 0;
  Status Consume(AudioPacket&& pkt) override = 0;

  Status Flush() { return Status::kOk; }
  Status Finish() override { return Status::kOk; }  // EOS / teardown
};

}  // namespace codec
}  // namespace video
