// packet_consumer.h
#pragma once

#include "core/status.h"
#include "core/types.h"

namespace video {
namespace codec {

// Transport-agnostic consumer of encoded packets. Both FileSinkConsumer and
// StreamConsumer implement this; swapping one for the other is a one-line
// change and the encoder never knows which is attached.
class PacketConsumer {
 public:
  virtual ~PacketConsumer() = default;
  // Video and audio both arrive as Packet; the packet's PacketType tells the
  // consumer which media it is. Consumers that only handle one media return
  // kUnsupportedOperation for the other.
  virtual StatusCode Consume(Packet&& pkt) = 0;
  virtual StatusCode Flush() { return StatusCode::kOk; }
  virtual StatusCode Finish() { return StatusCode::kOk; }  // EOS / teardown
};

}  // namespace codec
}  // namespace video
