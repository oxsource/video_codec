// packet_queue.cc
#include "queue/packet_queue.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "core/log_slot.h"

namespace video {
namespace codec {

// A single-producer / single-consumer bounded ring buffer. One producer (the
// encoder) pushes via OutputSink; one consumer drains via PacketSource (Next /
// Await). Packets are MOVED in and out of pre-allocated slots, so there is no
// per-packet heap allocation on the hot path.
//
// Note on "lock-free": the move-in / move-out and the full/empty checks are
// guarded by a single mutex that is only ever contended at the empty/full
// boundary (the blocking wait). This is correct for the strict SPSC pairing the
// architecture assumes; a fully lock-free blocking variant can be layered on
// later without changing these interfaces.
template <typename Pkt>
class Ring {
 public:
  Ring(size_t capacity, Backpressure policy)
      : capacity_(capacity),
        mask_(capacity - 1),
        policy_(policy),
        slots_(capacity) {
    // Caller guarantees capacity is a power of two (> 0).
  }

  // Returns true if the packet was accepted. Under kError, a full ring rejects
  // (returns false); under kDropOldest the oldest slot is overwritten; under
  // kBlock it waits. Pushing after MarkEos() is rejected.
  bool Push(Pkt&& pkt) {
    std::unique_lock<std::mutex> lk(mu_);
    if (eos_) return false;
    if (count_ == capacity_) {
      if (policy_ == Backpressure::kError) return false;
      if (policy_ == Backpressure::kDropOldest) {
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

  // Returns kOk (packet in `out`), kEmpty (timed out / non-blocking empty), or
  // kEos (end-of-stream and fully drained).
  Status Pop(Pkt& out, int64_t deadline_us) {
    std::unique_lock<std::mutex> lk(mu_);
    if (deadline_us > 0) {
      bool signaled =
          not_empty_.wait_for(lk, std::chrono::microseconds(deadline_us),
                              [this] { return count_ > 0 || eos_; });
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

  void MarkEos() {
    std::lock_guard<std::mutex> lk(mu_);
    eos_ = true;
    not_empty_.notify_all();
  }

  size_t size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return count_;
  }

  size_t capacity() const { return capacity_; }
  Backpressure policy() const { return policy_; }
  bool eos() const {
    std::lock_guard<std::mutex> lk(mu_);
    return eos_ && count_ == 0;
  }

 private:
  const size_t capacity_;
  const size_t mask_;
  const Backpressure policy_;
  std::vector<Pkt> slots_;
  size_t head_ = 0;  // consumer index
  size_t tail_ = 0;  // producer index
  size_t count_ = 0;
  bool eos_ = false;
  mutable std::mutex mu_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
};

// Explicit instantiation: only these two packet types are used.
template class Ring<VideoPacket>;
template class Ring<AudioPacket>;

namespace {

// Drain one packet of media type `Pkt` from `src` and forward it to `sink`.
// Returns false only when the consumer failed (Finish() already called) — the
// whole await must abort. Sets `done` when that media's stream hit EOS; a done
// stream is a no-op so the loop can call this unconditionally.
template <typename Pkt>
bool DrainOne(PacketSource& src, Pkt& out, bool& done, PacketSink& sink,
              int64_t deadline_us, const char* what) {
  if (done) return true;
  switch (src.Next(out, deadline_us)) {
    case Status::kOk:
      if (sink.Consume(std::move(out)) != Status::kOk) {
        VC_LOG(LogLevel::kError,
               std::string("PacketQueue::Await: ") + what + " Consume failed");
        sink.Finish();
        return false;
      }
      return true;
    case Status::kEos:
      done = true;
      return true;
    case Status::kEmpty:
      return true;  // try the other media (or retry next iteration)
  }
  return true;  // Next only yields kOk/kEmpty/kEos; keep draining otherwise
}

}  // namespace

PacketQueue::PacketQueue(size_t capacity, Backpressure policy)
    : video_(std::make_unique<Ring<VideoPacket>>(capacity, policy)),
      audio_(std::make_unique<Ring<AudioPacket>>(capacity, policy)) {}

PacketQueue::~PacketQueue() = default;  // Ring is complete here

Status PacketQueue::Submit(VideoPacket&& pkt) {
  if (video_->Push(std::move(pkt))) return Status::kOk;
  // Rejected: full under kError (kBackendUnavailable), or push-after-EOS
  // (kInvalidArgument). Under kBlock/kDropOldest Push only fails at EOS.
  return video_->policy() == Backpressure::kError ? Status::kBackendUnavailable
                                                  : Status::kInvalidArgument;
}

Status PacketQueue::Submit(AudioPacket&& pkt) {
  if (audio_->Push(std::move(pkt))) return Status::kOk;
  return audio_->policy() == Backpressure::kError ? Status::kBackendUnavailable
                                                  : Status::kInvalidArgument;
}

Status PacketQueue::Next(VideoPacket& out, int64_t deadline_us) {
  return video_->Pop(out, deadline_us);
}

Status PacketQueue::Next(AudioPacket& out, int64_t deadline_us) {
  return audio_->Pop(out, deadline_us);
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
  video_->MarkEos();
  audio_->MarkEos();
}

size_t PacketQueue::capacity() const { return video_->capacity(); }

size_t PacketQueue::size() const { return video_->size() + audio_->size(); }

}  // namespace codec
}  // namespace video
