// encoded_packet_queue.cc
#include "queue/encoded_packet_queue.h"

namespace video {
namespace codec {

EncodedPacketQueue::EncodedPacketQueue(size_t capacity, Backpressure policy)
    : video_(capacity, policy), audio_(capacity, policy) {}

StatusCode EncodedPacketQueue::Submit(EncodedPacket&& pkt) {
  if (video_.Push(std::move(pkt))) return StatusCode::kOk;
  // Rejected: full under kError (kBackendUnavailable), or push-after-EOS
  // (kInvalidArgument). Under kBlock/kDropOldest Push only fails at EOS.
  return video_.policy() == Backpressure::kError ? StatusCode::kBackendUnavailable
                                                 : StatusCode::kInvalidArgument;
}

StatusCode EncodedPacketQueue::Submit(AudioPacket&& pkt) {
  if (audio_.Push(std::move(pkt))) return StatusCode::kOk;
  return audio_.policy() == Backpressure::kError ? StatusCode::kBackendUnavailable
                                                 : StatusCode::kInvalidArgument;
}

PopResult EncodedPacketQueue::Pop(EncodedPacket& out, int64_t deadline_us) {
  return video_.Pop(out, deadline_us);
}

PopResult EncodedPacketQueue::Pop(AudioPacket& out, int64_t deadline_us) {
  return audio_.Pop(out, deadline_us);
}

void EncodedPacketQueue::MarkEos() {
  video_.MarkEos();
  audio_.MarkEos();
}

}  // namespace codec
}  // namespace video
