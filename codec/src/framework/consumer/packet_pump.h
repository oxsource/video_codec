// packet_pump.h
#pragma once

#include "consumer/packet_consumer.h"
#include "queue/queue_iface.h"

namespace video {
namespace codec {

// Bridges an EncodedPacketSource (the ring buffer) to a PacketConsumer on the
// consumer thread. The loop pops blocking (per `deadline_us`) and forwards each
// packet to the consumer, calling Finish() at EOS. It must NOT swallow consumer
// errors and spin — a failing Consume is logged and stops the pump.
class PacketPump {
 public:
  static void Run(EncodedPacketSource& src, PacketConsumer& consumer,
                   int64_t deadline_us = 100'000);
};

}  // namespace codec
}  // namespace video
