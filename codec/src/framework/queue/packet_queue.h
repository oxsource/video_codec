// packet_queue.h
#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>

#include "core/types.h"
#include "queue/queue_iface.h"

namespace video {
namespace codec {

// A single-producer / single-consumer bounded ring buffer. One producer (the
// encoder) pushes via OutputSink; one consumer drains via PacketSource (Pop /
// Await). Packets are MOVED in and out of pre-allocated slots, so
// there is no per-packet heap allocation on the hot path.
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

// Bounded SPSC ring buffer implementing both transport endpoints. Video and
// audio are distinct packet types stored on INDEPENDENT rings: back-pressure
// and drain are per-media, so a stall on one does not block the other.
class PacketQueue : public OutputSink, public PacketSource {
 public:
  // `capacity` MUST be > 0 and a power of two (index masking).
  PacketQueue(size_t capacity, Backpressure policy = Backpressure::kBlock);

  // OutputSink (producer).
  Status Submit(VideoPacket&& pkt) override;
  Status Submit(AudioPacket&& pkt) override;
  Status Flush() override { return Status::kOk; }

  // PacketSource (consumer). Each ring blocks/drains independently.
  Status Pop(VideoPacket& out, int64_t deadline_us) override;
  Status Pop(AudioPacket& out, int64_t deadline_us) override;

  // Await mechanism: block on the calling thread, delivering every packet
  // (video and audio, drained alternately) to `sink` until EOS.
  Status Await(PacketSink& sink, int64_t deadline_us = 100'000) override;

  void MarkEos() override;

  size_t capacity() const { return video_.capacity(); }
  size_t size() const { return video_.size() + audio_.size(); }

 private:
  Ring<VideoPacket> video_;
  Ring<AudioPacket> audio_;
};

}  // namespace codec
}  // namespace video
