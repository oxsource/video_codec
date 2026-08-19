// packet_queue.cc
#include "src/framework/queue/packet_queue.h"

#include <cassert>
#include <chrono>
#include <string>

#include "src/framework/core/log_slot.h"

namespace video {
namespace codec {

template <typename Pkt>
Ring<Pkt>::Ring(size_t capacity, Backpressure policy)
    : capacity_(capacity), mask_(capacity - 1), policy_(policy), slots_(capacity) {
  // A non-power-of-two capacity corrupts index masking (data loss); capacity 0
  // deadlocks under kBlock and is UB under kLatest (empty slots_).
  assert(capacity > 0 && (capacity & (capacity - 1)) == 0);
}

template <typename Pkt>
bool Ring<Pkt>::Push(Pkt&& pkt) {
  std::unique_lock<std::mutex> lk(mu_);
  if (eos_) return false;
  if (count_ == capacity_) {
    if (policy_ == Backpressure::kError) return false;
    if (policy_ == Backpressure::kLatest) {
      slots_[head_] = Pkt{};
      head_ = (head_ + 1) & mask_;
      --count_;
    } else {  // kBlock
      not_full_.wait(lk, [this] { return count_ < capacity_ || eos_; });
      if (eos_) return false;
    }
  }
  slots_[tail_] = std::move(pkt);
  tail_ = (tail_ + 1) & mask_;
  ++count_;
  not_empty_.notify_one();
  return true;
}

template <typename Pkt>
Status Ring<Pkt>::Pop(Pkt& out, int64_t deadline_us) {
  std::unique_lock<std::mutex> lk(mu_);
  if (deadline_us > 0) {
    auto ts = std::chrono::microseconds(deadline_us);
    bool signaled = not_empty_.wait_for(lk, ts, [this] { return count_ > 0 || eos_; });
    if (!signaled && count_ == 0) return Status::kEmpty;
  } else if (count_ == 0) {
    return eos_ ? Status::kEos : Status::kEmpty;
  }
  if (count_ == 0) return Status::kEos;
  out = std::move(slots_[head_]);
  slots_[head_] = Pkt{};
  head_ = (head_ + 1) & mask_;
  --count_;
  not_full_.notify_one();
  return Status::kOk;
}

template <typename Pkt>
void Ring<Pkt>::MarkEos() {
  std::lock_guard<std::mutex> lk(mu_);
  eos_ = true;
  not_empty_.notify_all();
}

template <typename Pkt>
size_t Ring<Pkt>::size() const {
  std::lock_guard<std::mutex> lk(mu_);
  return count_;
}

template <typename Pkt>
size_t Ring<Pkt>::capacity() const {
  return capacity_;
}

template <typename Pkt>
Backpressure Ring<Pkt>::policy() const {
  return policy_;
}

template <typename Pkt>
bool Ring<Pkt>::eos() const {
  std::lock_guard<std::mutex> lk(mu_);
  return eos_ && count_ == 0;
}

// Explicit instantiation: only these two packet types are used.
template class Ring<VideoPacket>;
template class Ring<AudioPacket>;

namespace {

// Drain one packet of media type `Pkt` from `src` and forward it to `sink`.
// Returns false only when the consumer failed (Finish() already called) — the
// whole await must abort. Sets `done` when that media's stream hit EOS; a done
// stream is a no-op so the loop can call this unconditionally.
template <typename Pkt>
bool DrainOne(PacketSource& src, Pkt& out, bool& done, PacketSink& sink, int64_t deadline_us,
              const char* what) {
  if (done) return true;
  switch (src.Pull(out, deadline_us)) {
    case Status::kOk:
      if (sink.Push(std::move(out)) != Status::kOk) {
        VC_LOG(LogLevel::kError, std::string("PacketQueue::Await: ") + what + " Push failed");
        sink.Finish();
        return false;
      }
      return true;
    case Status::kEos:
      done = true;
      return true;
    case Status::kEmpty:
      return true;  // try the other media (or retry next iteration)
    default:
      // Pull only yields kOk/kEmpty/kEos; keep draining on anything else.
      return true;
  }
}

}  // namespace

PacketQueue::PacketQueue(size_t capacity, Backpressure policy)
    : video_(capacity, policy), audio_(capacity, policy) {}

Status PacketQueue::Push(VideoPacket&& pkt) {
  if (video_.Push(std::move(pkt))) return Status::kOk;
  // Rejected: full under kError (kBackendUnavailable), or push-after-EOS
  // (kInvalidArgument). Under kBlock/kLatest Push only fails at EOS.
  return video_.policy() == Backpressure::kError ? Status::kBackendUnavailable
                                                 : Status::kInvalidArgument;
}

Status PacketQueue::Push(AudioPacket&& pkt) {
  if (audio_.Push(std::move(pkt))) return Status::kOk;
  return audio_.policy() == Backpressure::kError ? Status::kBackendUnavailable
                                                 : Status::kInvalidArgument;
}

Status PacketQueue::Pull(VideoPacket& out, int64_t deadline_us) {
  return video_.Pop(out, deadline_us);
}

Status PacketQueue::Pull(AudioPacket& out, int64_t deadline_us) {
  return audio_.Pop(out, deadline_us);
}

Status PacketQueue::Await(PacketSink& sink, int64_t deadline_us) {
  VideoPacket vp;
  AudioPacket ap;
  bool video_done = false;
  bool audio_done = false;

  // DrainOne no-ops on a done stream, so the loop calls both unconditionally;
  // termination is the explicit break once BOTH streams hit EOS.
  while (true) {
    if (!DrainOne(*this, vp, video_done, sink, deadline_us, "video") ||
        !DrainOne(*this, ap, audio_done, sink, deadline_us, "audio")) {
      return Status::kEncodeFailed;
    }
    if (video_done && audio_done) break;  // both streams finished
  }
  return sink.Finish();
}

void PacketQueue::MarkEos() {
  video_.MarkEos();
  audio_.MarkEos();
}

}  // namespace codec
}  // namespace video
