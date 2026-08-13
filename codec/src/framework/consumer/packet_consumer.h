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
  virtual StatusCode Consume(EncodedPacket&& pkt) = 0;  // video
  virtual StatusCode Consume(AudioPacket&& pkt) = 0;    // audio
  virtual StatusCode Flush() { return StatusCode::kOk; }
  virtual StatusCode Finish() { return StatusCode::kOk; }  // EOS / teardown
};

}  // namespace codec
}  // namespace video
