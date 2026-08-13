// mp4_file_consumer.cc
#include "consumer/mp4_file_consumer.h"

#include <utility>

namespace video {
namespace codec {

Mp4FileConsumer::Mp4FileConsumer(std::string path, int width, int height,
                                 int fps)
    : sink_(std::move(path)), muxer_(&sink_, width, height, fps) {}

Status Mp4FileConsumer::Consume(Packet&& pkt) {
  if (pkt.type == PacketType::kAudio) {
    return Status::kUnsupportedOperation;  // video-only muxer
  }
  return muxer_.Consume(pkt);
}

Status Mp4FileConsumer::Finish() { return muxer_.Finish(); }

}  // namespace codec
}  // namespace video
