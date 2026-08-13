// packet_queue.cc
#include "queue/packet_queue.h"

#include <chrono>
#include <string>

#include "core/log_slot.h"

namespace video {
namespace codec {

PacketQueue::PacketQueue(size_t capacity, Backpressure policy)
    : video_(capacity, policy), audio_(capacity, policy) {}

Status PacketQueue::Submit(Packet&& pkt) {
  Ring<Packet>& ring = (pkt.type == PacketType::kAudio) ? audio_ : video_;
  const bool accepted = ring.Push(std::move(pkt));
  {
    std::lock_guard<std::mutex> lk(qmu_);
    qnot_empty_.notify_one();
  }
  if (accepted) return Status::kOk;
  // Rejected: full under kError (kBackendUnavailable), or push-after-EOS
  // (kInvalidArgument). Under kBlock/kDropOldest Push only fails at EOS.
  return ring.policy() == Backpressure::kError ? Status::kBackendUnavailable
                                               : Status::kInvalidArgument;
}

PacketSource::PopResult PacketQueue::TryPop(Packet& out) {
  // Round-robin: alternate which ring is tried first so neither media starves
  // when both are busy.
  const bool video_first = prefer_video_;
  prefer_video_ = !prefer_video_;

  const PacketSource::PopResult r1 =
      video_first ? video_.Pop(out, 0) : audio_.Pop(out, 0);
  if (r1 == PacketSource::PopResult::kOk) return PacketSource::PopResult::kOk;
  const PacketSource::PopResult r2 =
      video_first ? audio_.Pop(out, 0) : video_.Pop(out, 0);
  if (r2 == PacketSource::PopResult::kOk) return PacketSource::PopResult::kOk;

  // Both empty: EOS only when both rings are finished and drained.
  return (video_.eos() && audio_.eos()) ? PacketSource::PopResult::kEos
                                        : PacketSource::PopResult::kEmpty;
}

PacketSource::PopResult PacketQueue::Pop(Packet& out, int64_t deadline_us) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::microseconds(deadline_us);
  std::unique_lock<std::mutex> lk(qmu_);
  for (;;) {
    const PacketSource::PopResult r = TryPop(out);
    if (r != PacketSource::PopResult::kEmpty) return r;
    if (deadline_us <= 0) return PacketSource::PopResult::kEmpty;
    if (std::chrono::steady_clock::now() >= deadline)
      return PacketSource::PopResult::kEmpty;
    qnot_empty_.wait_until(lk, deadline);
  }
}

Status PacketQueue::Await(PacketSink& sink, int64_t deadline_us) {
  Packet pkt;
  for (;;) {
    switch (Pop(pkt, deadline_us)) {
      case PacketSource::PopResult::kOk:
        if (sink.Consume(std::move(pkt)) != Status::kOk) {
          VC_LOG(LogLevel::kError, "PacketQueue::Await: Consume failed");
          sink.Finish();
          return Status::kEncodeFailed;
        }
        break;
      case PacketSource::PopResult::kEos:
        return sink.Finish();  // drained: propagate the sink's teardown status
      case PacketSource::PopResult::kEmpty:
        break;  // retry (deadline expired, not yet EOS)
    }
  }
}

void PacketQueue::MarkEos() {
  video_.MarkEos();
  audio_.MarkEos();
  std::lock_guard<std::mutex> lk(qmu_);
  qnot_empty_.notify_all();
}

}  // namespace codec
}  // namespace video
