// file_sink_consumer.h
#pragma once

#include <fstream>
#include <string>

#include "consumer/packet_consumer.h"

namespace video {
namespace codec {

// Writes encoded packets to files: Annex-B video to `video_path`, ADTS audio
// to `audio_path` (if provided). Routing is by PacketType. Order and keyframe
// boundaries are preserved because packets arrive in order from the ring
// buffer. A muxer (`.mp4`) is just another PacketConsumer behind the same
// interface.
class FileSinkConsumer : public PacketConsumer {
 public:
  // `video_path` is required; `audio_path` is optional.
  explicit FileSinkConsumer(std::string video_path,
                            std::string audio_path = "");

  Status Consume(Packet&& pkt) override;
  Status Finish() override;  // flush + close

 private:
  std::string video_path_;
  std::string audio_path_;
  std::ofstream video_file_;
  std::ofstream audio_file_;
};

}  // namespace codec
}  // namespace video
