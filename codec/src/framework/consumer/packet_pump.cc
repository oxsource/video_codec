// packet_pump.cc
#include "consumer/packet_pump.h"

#include "core/log_slot.h"

namespace video {
namespace codec {

void PacketPump::Run(EncodedPacketSource& src, PacketConsumer& consumer,
                      int64_t deadline_us) {
  EncodedPacket vp;
  AudioPacket ap;
  bool video_done = false;
  bool audio_done = false;

  while (!video_done || !audio_done) {
    if (!video_done) {
      PopResult r = src.Pop(vp, deadline_us);
      if (r == PopResult::kOk) {
        StatusCode s = consumer.Consume(std::move(vp));
        if (s != StatusCode::kOk) {
          VC_LOG(LogLevel::kError, "PacketPump: video Consume failed");
          consumer.Finish();
          return;
        }
      } else if (r == PopResult::kEos) {
        video_done = true;
      }
      // kEmpty: try audio below (or block next iteration).
    }

    if (!audio_done) {
      PopResult r = src.Pop(ap, deadline_us);
      if (r == PopResult::kOk) {
        StatusCode s = consumer.Consume(std::move(ap));
        if (s != StatusCode::kOk) {
          VC_LOG(LogLevel::kError, "PacketPump: audio Consume failed");
          consumer.Finish();
          return;
        }
      } else if (r == PopResult::kEos) {
        audio_done = true;
      }
    }
  }

  consumer.Finish();
}

}  // namespace codec
}  // namespace video
