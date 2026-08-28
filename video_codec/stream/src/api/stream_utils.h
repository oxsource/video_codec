#pragma once

#include <cstdint>
#include <vector>

namespace video {
namespace codec {
struct VideoPacket;
}  // namespace codec
}  // namespace video

namespace video {
namespace stream {

// Build an H.264 Annex-B SEI NAL unit suitable as a "dummy frame" to keep
// remote servers (MediaMTX, etc.) from hitting their "no tracks" timeout
// while the real encoder pipeline is still being set up.
//
// Usage:
//   auto sei = MakeSeiFrame();
//   stream->SendVideo(sei);
//
inline video::codec::VideoPacket MakeSeiFrame() {
  video::codec::VideoPacket pkt;
  // SEI NAL (type 6) with a single payload of 0x01, 0x02, 0x03.
  pkt.data = {0x00, 0x00, 0x00, 0x01, 0x06, 0x01, 0x02, 0x03};
  pkt.pts_us = 1;
  return pkt;
}

}  // namespace stream
}  // namespace video