// mp4_file_consumer.cc
#include "mux/mp4_file_consumer.h"

#include <utility>

namespace video {
namespace codec {

Mp4FileConsumer::Mp4FileConsumer(std::string path, int width, int height,
                                 int fps)
    : sink_(std::move(path)), muxer_(&sink_, width, height, fps) {}

StatusCode Mp4FileConsumer::Consume(EncodedPacket&& pkt) {
  return muxer_.Consume(pkt);
}

StatusCode Mp4FileConsumer::Finish() { return muxer_.Finish(); }

}  // namespace codec
}  // namespace video
