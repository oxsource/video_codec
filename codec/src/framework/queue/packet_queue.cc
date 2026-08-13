// packet_queue.cc
#include "queue/packet_queue.h"

#include <string>

#include "core/log_slot.h"

namespace video {
namespace codec {

PacketQueue::PacketQueue(size_t capacity, Backpressure policy)
    : video_(capacity, policy), audio_(capacity, policy) {}

Status PacketQueue::Submit(VideoPacket&& pkt) {
  if (video_.Push(std::move(pkt))) return Status::kOk;
  // Rejected: full under kError (kBackendUnavailable), or push-after-EOS
  // (kInvalidArgument). Under kBlock/kDropOldest Push only fails at EOS.
  return video_.policy() == Backpressure::kError ? Status::kBackendUnavailable
                                                 : Status::kInvalidArgument;
}

Status PacketQueue::Submit(AudioPacket&& pkt) {
  if (audio_.Push(std::move(pkt))) return Status::kOk;
  return audio_.policy() == Backpressure::kError ? Status::kBackendUnavailable
                                                 : Status::kInvalidArgument;
}

Status PacketQueue::Pop(VideoPacket& out, int64_t deadline_us) {
  return video_.Pop(out, deadline_us);
}

Status PacketQueue::Pop(AudioPacket& out, int64_t deadline_us) {
  return audio_.Pop(out, deadline_us);
}

Status PacketQueue::Await(PacketSink& sink, int64_t deadline_us) {
  VideoPacket vp;
  AudioPacket ap;
  bool video_done = false;
  bool audio_done = false;

  while (!video_done || !audio_done) {
    if (!video_done) {
      switch (Pop(vp, deadline_us)) {
        case Status::kOk:
          if (sink.Consume(std::move(vp)) != Status::kOk) {
            VC_LOG(LogLevel::kError,
                   "PacketQueue::Await: video Consume failed");
            sink.Finish();
            return Status::kEncodeFailed;
          }
          break;
        case Status::kEos:
          video_done = true;
          break;
        case Status::kEmpty:
          break;  // try audio below (or retry next iteration)
      }
    }
    if (!audio_done) {
      switch (Pop(ap, deadline_us)) {
        case Status::kOk:
          if (sink.Consume(std::move(ap)) != Status::kOk) {
            VC_LOG(LogLevel::kError,
                   "PacketQueue::Await: audio Consume failed");
            sink.Finish();
            return Status::kEncodeFailed;
          }
          break;
        case Status::kEos:
          audio_done = true;
          break;
        case Status::kEmpty:
          break;
      }
    }
  }
  return sink.Finish();
}

void PacketQueue::MarkEos() {
  video_.MarkEos();
  audio_.MarkEos();
}

}  // namespace codec
}  // namespace video
