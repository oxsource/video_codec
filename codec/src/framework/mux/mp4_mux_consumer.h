// mp4_mux_consumer.h
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "consumer/packet_consumer.h"
#include "core/types.h"

struct AVFormatContext;

namespace video {
namespace codec {

// Muxes encoded H.264 packets into an MP4 file using FFmpeg's libavformat
// (mov/mp4 muxer). A PacketConsumer behind the standard interface, so the
// encoder -> queue -> PacketPump pipeline is unchanged.
//
// The encoder emits Annex-B samples (start codes, SPS/PPS inline). This
// consumer converts them to length-prefixed (AVCC) samples, builds the avcC
// extradata from the first keyframe's SPS/PPS, and writes a standard MP4.
// Geometry/framerate come from the encoder config (fallbacks if SPS parsing
// yields no dimensions).
class Mp4MuxConsumer : public PacketConsumer {
 public:
  // `fps` drives the stream time_base; `width`/`height` are fallbacks for the
  // stream header (the SPS also carries dimensions, but parsing them is out of
  // scope here).
  explicit Mp4MuxConsumer(std::string path, int width = 0, int height = 0,
                          int fps = 30);
  ~Mp4MuxConsumer() override;

  StatusCode Consume(EncodedPacket&& pkt) override;
  StatusCode Consume(AudioPacket&& pkt) override {
    return StatusCode::kUnsupportedOperation;  // video-only muxer
  }
  StatusCode Finish() override;

 private:
  // Open the mp4 muxer lazily on the first keyframe (needs SPS/PPS).
  StatusCode OpenMuxer(const EncodedPacket& first_keyframe);

  // Parse Annex-B SPS/PPS into an avcC (length-prefixed) extradata block.
  StatusCode BuildExtradata(const uint8_t* data, size_t size,
                            std::vector<uint8_t>* out);

  // Convert one Annex-B packet to AVCC (4-byte lengths, SPS/PPS dropped).
  StatusCode AnnexBToAvcc(const uint8_t* data, size_t size,
                          std::vector<uint8_t>* out);

  std::string path_;
  int width_;
  int height_;
  int fps_;
  AVFormatContext* fmt_ = nullptr;
  int stream_index_ = -1;
  bool opened_ = false;
};

}  // namespace codec
}  // namespace video
