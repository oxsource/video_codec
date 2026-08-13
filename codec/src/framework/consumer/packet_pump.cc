// packet_pump.cc
#include "consumer/packet_pump.h"

#include <string>

#include "core/log_slot.h"

namespace video {
namespace codec {

namespace {

// One output stream being drained (video or audio). Encapsulates the packet
// buffer and the finished flag so the pump loop stays flat.
template <typename Packet>
struct StreamChannel {
  Packet packet;
  bool done = false;

  // Pop one packet and forward it. Returns false only when the consumer
  // failed (Finish() already called) — the pump must stop entirely.
  bool Drain(EncodedPacketSource& src, PacketConsumer& consumer,
             int64_t deadline_us, const char* what) {
    if (done) return true;
    const PopResult r = src.Pop(packet, deadline_us);
    if (r == PopResult::kEos) {
      done = true;
      return true;
    }
    if (r == PopResult::kEmpty) return true;
    if (consumer.Consume(std::move(packet)) != StatusCode::kOk) {
      VC_LOG(LogLevel::kError,
             std::string("PacketPump: ") + what + " Consume failed");
      consumer.Finish();
      return false;
    }
    return true;
  }
};

}  // namespace

void PacketPump::Run(EncodedPacketSource& src, PacketConsumer& consumer,
                     int64_t deadline_us) {
  StreamChannel<EncodedPacket> video;
  StreamChannel<AudioPacket> audio;

  while (true) {
    if (!video.Drain(src, consumer, deadline_us, "video")) return;  // abort
    if (!audio.Drain(src, consumer, deadline_us, "audio")) return;  // abort
    if (video.done && audio.done) break;  // both streams finished
  }
  consumer.Finish();
}

}  // namespace codec
}  // namespace video
