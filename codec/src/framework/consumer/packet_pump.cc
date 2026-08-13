// packet_pump.cc
#include "consumer/packet_pump.h"

#include <string>

#include "core/log_slot.h"

namespace video {
namespace codec {

void PacketPump::Run(PacketSource& src, PacketConsumer& consumer,
                     int64_t deadline_us) {
  Packet pkt;
  while (true) {
    switch (src.Pop(pkt, deadline_us)) {
      case PopResult::kOk:
        if (consumer.Consume(std::move(pkt)) != StatusCode::kOk) {
          VC_LOG(LogLevel::kError, "PacketPump: Consume failed");
          consumer.Finish();
          return;
        }
        break;
      case PopResult::kEos:
        consumer.Finish();
        return;
      case PopResult::kEmpty:
        break;  // try again (deadline expired or not yet EOS)
    }
  }
}

}  // namespace codec
}  // namespace video
