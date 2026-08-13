// mp4_consumer.cc
#include "consumer/mp4_consumer.h"

namespace video {
namespace codec {

Mp4Consumer::Mp4Consumer(ByteSink* sink, int width, int height, int fps,
                         const MuxOptions& options)
    : sink_(sink), muxer_(sink_, width, height, fps, options) {}

Status Mp4Consumer::Push(VideoPacket&& pkt) { return muxer_.Consume(pkt); }

Status Mp4Consumer::Finish() { return muxer_.Finish(); }

}  // namespace codec
}  // namespace video
