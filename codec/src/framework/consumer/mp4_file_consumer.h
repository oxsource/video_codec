// mp4_file_consumer.h
#pragma once

#include <string>

#include "consumer/file_byte_sink.h"
#include "consumer/packet_consumer.h"
#include "core/types.h"
#include "mux/mp4_muxer.h"

namespace video {
namespace codec {

// A PacketConsumer that muxes encoded H.264 packets into an MP4 FILE. A thin
// composition: FileByteSink owns the file I/O, Mp4Muxer does the format
// conversion — neither is entangled with the other.
class Mp4FileConsumer : public PacketConsumer {
 public:
  // `fps` drives the stream time_base; `width`/`height` are fallbacks for the
  // stream header.
  explicit Mp4FileConsumer(std::string path, int width = 0, int height = 0,
                           int fps = 30);
  ~Mp4FileConsumer() override = default;

  Status Consume(VideoPacket&& pkt) override;
  Status Consume(AudioPacket&& pkt) override {
    return Status::kUnsupportedOperation;  // video-only muxer
  }
  Status Finish() override;

 private:
  FileByteSink sink_;
  Mp4Muxer muxer_;
};

}  // namespace codec
}  // namespace video
