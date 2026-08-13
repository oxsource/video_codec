// packet_queue.h
#pragma once

#include <cstddef>
#include <memory>

#include "core/types.h"
#include "queue/queue_iface.h"

namespace video {
namespace codec {

// Bounded SPSC ring buffer. Implementation lives in packet_queue.cc: it is a
// template, and only the VideoPacket/AudioPacket instantiations are used, so
// the header only forward-declares it (members are PIMPL'd unique_ptrs).
template <typename Pkt>
class Ring;

// Bounded SPSC ring buffer implementing both transport endpoints. Video and
// audio are distinct packet types stored on INDEPENDENT rings: back-pressure
// and drain are per-media, so a stall on one does not block the other.
class PacketQueue : public OutputSink, public PacketSource {
 public:
  // `capacity` MUST be > 0 and a power of two (index masking).
  PacketQueue(size_t capacity, Backpressure policy = Backpressure::kBlock);
  ~PacketQueue() override;  // Ring is complete only in packet_queue.cc

  // OutputSink (producer).
  Status Submit(VideoPacket&& pkt) override;
  Status Submit(AudioPacket&& pkt) override;
  Status Flush() override { return Status::kOk; }

  // PacketSource (consumer). Each ring blocks/drains independently.
  Status Next(VideoPacket& out, int64_t deadline_us) override;
  Status Next(AudioPacket& out, int64_t deadline_us) override;

  // Await mechanism: block on the calling thread, delivering every packet
  // (video and audio, drained alternately) to `sink` until EOS.
  Status Await(PacketSink& sink, int64_t deadline_us = 100'000) override;

  void MarkEos() override;

  size_t capacity() const;
  size_t size() const;

 private:
  std::unique_ptr<Ring<VideoPacket>> video_;
  std::unique_ptr<Ring<AudioPacket>> audio_;
};

}  // namespace codec
}  // namespace video
