// packet_queue.cc
#include "queue/packet_queue.h"

#include <chrono>

namespace video {
namespace codec {

PacketQueue::PacketQueue(size_t capacity, Backpressure policy)
    : video_(capacity, policy), audio_(capacity, policy) {}

StatusCode PacketQueue::Submit(Packet&& pkt) {
  Ring<Packet>& ring = (pkt.type == PacketType::kAudio) ? audio_ : video_;
  const bool accepted = ring.Push(std::move(pkt));
  {
    std::lock_guard<std::mutex> lk(qmu_);
    qnot_empty_.notify_one();
  }
  if (accepted) return StatusCode::kOk;
  // Rejected: full under kError (kBackendUnavailable), or push-after-EOS
  // (kInvalidArgument). Under kBlock/kDropOldest Push only fails at EOS.
  return ring.policy() == Backpressure::kError ? StatusCode::kBackendUnavailable
                                               : StatusCode::kInvalidArgument;
}

PopResult PacketQueue::TryPop(Packet& out) {
  // Round-robin: alternate which ring is tried first so neither media starves
  // when both are busy.
  const bool video_first = prefer_video_;
  prefer_video_ = !prefer_video_;

  const PopResult r1 = video_first ? video_.Pop(out, 0) : audio_.Pop(out, 0);
  if (r1 == PopResult::kOk) return PopResult::kOk;
  const PopResult r2 = video_first ? audio_.Pop(out, 0) : video_.Pop(out, 0);
  if (r2 == PopResult::kOk) return PopResult::kOk;

  // Both empty: EOS only when both rings are finished and drained.
  return (video_.eos() && audio_.eos()) ? PopResult::kEos : PopResult::kEmpty;
}

PopResult PacketQueue::Pop(Packet& out, int64_t deadline_us) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::microseconds(deadline_us);
  std::unique_lock<std::mutex> lk(qmu_);
  for (;;) {
    const PopResult r = TryPop(out);
    if (r != PopResult::kEmpty) return r;
    if (deadline_us <= 0) return PopResult::kEmpty;
    if (std::chrono::steady_clock::now() >= deadline) return PopResult::kEmpty;
    qnot_empty_.wait_until(lk, deadline);
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
