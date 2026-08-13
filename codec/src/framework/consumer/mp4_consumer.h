// mp4_consumer.h
#pragma once

#include "consumer/packet_consumer.h"
#include "core/types.h"
#include "io/byte_sink.h"
#include "mux/mp4_muxer.h"

namespace video {
namespace codec {

// A PacketConsumer that muxes encoded H.264 packets into an MP4 container,
// writing every byte through a caller-supplied ByteSink (file, network
// stream, tee of both...). A thin composition: the ByteSink owns the I/O,
// Mp4Muxer does the format conversion — neither is entangled with the other.
//
// By default the muxer produces fragmented MP4 (per-keyframe fragments), so
// output is sequential and each fragment is committed to the sink as it
// completes — safe for unstable write targets (removable media) and streamable
// to network/cloud sinks.
class Mp4Consumer : public PacketConsumer {
 public:
  // `sink` must outlive this consumer. `fps` drives the stream time_base;
  // `width`/`height` are fallbacks for the stream header.
  explicit Mp4Consumer(ByteSink* sink, int width = 0, int height = 0,
                       int fps = 30, const MuxOptions& options = MuxOptions());
  ~Mp4Consumer() override = default;

  Status Consume(VideoPacket&& pkt) override;
  Status Consume(AudioPacket&& pkt) override {
    return Status::kUnsupportedOperation;  // video-only muxer
  }
  Status Finish() override;

 private:
  ByteSink* sink_;  // non-owning
  Mp4Muxer muxer_;
};

}  // namespace codec
}  // namespace video
