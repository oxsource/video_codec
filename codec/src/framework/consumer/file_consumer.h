// file_consumer.h
#pragma once

#include <memory>
#include <string>

#include "src/framework/consumer/packet_consumer.h"
#include "src/framework/io/file_byte_sink.h"

namespace video {
namespace codec {

// Writes encoded packets to files: Annex-B video to `video_path`, ADTS audio
// to `audio_path` (if provided). Routing is by the Push overload. Order and
// keyframe boundaries are preserved because packets arrive in order from the
// ring buffer. A muxer (`.mp4`) is just another PacketConsumer behind the same
// interface.
//
// Byte-level output is delegated to io::FileByteSink, so this class only
// adapts packets to a file: no FILE*/fstream handling of its own.
class FileConsumer : public PacketConsumer {
 public:
  // `video_path` is required; `audio_path` is optional.
  explicit FileConsumer(std::string video_path, std::string audio_path = "");

  Status Push(VideoPacket&& pkt) override;
  Status Push(AudioPacket&& pkt) override;
  Status Finish() override;  // flush + close

 private:
  std::unique_ptr<FileByteSink> video_sink_;
  std::unique_ptr<FileByteSink> audio_sink_;
};

}  // namespace codec
}  // namespace video
