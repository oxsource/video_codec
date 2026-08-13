// mp4_muxer.h
#pragma once

#include <cstdint>
#include <vector>

#include "core/status.h"
#include "core/types.h"

struct AVFormatContext;

namespace video {
namespace codec {

class ByteSink;

// Muxes encoded H.264 Annex-B packets into the MP4 container (FFmpeg's mov
// muxer). Purely a format converter: every output byte is written through a
// caller-supplied ByteSink (file, memory buffer, ...) — this class never
// touches the filesystem.
//
// The encoder emits Annex-B samples (start codes, SPS/PPS inline). This muxer
// converts them to length-prefixed (AVCC) samples, builds the avcC extradata
// from the first keyframe's SPS/PPS, and writes a standard MP4. Geometry and
// framerate come from the encoder config (fallbacks if SPS parsing yields no
// dimensions).
class Mp4Muxer {
 public:
  // `sink` must outlive this muxer. `fps` drives the stream time_base;
  // `width`/`height` are fallbacks for the stream header.
  Mp4Muxer(ByteSink* sink, int width = 0, int height = 0, int fps = 30);
  ~Mp4Muxer();

  Mp4Muxer(const Mp4Muxer&) = delete;
  Mp4Muxer& operator=(const Mp4Muxer&) = delete;

  // Feed one encoded packet. The MP4 is opened lazily on the first keyframe
  // (it needs SPS/PPS for the avcC extradata); non-keyframes before that are
  // dropped.
  StatusCode Consume(const EncodedPacket& pkt);

  // Write the MP4 trailer and flush the sink. Safe to call once.
  StatusCode Finish();

 private:
  // Open the mp4 muxer lazily on the first keyframe (needs SPS/PPS).
  StatusCode OpenMuxer(const EncodedPacket& first_keyframe);

  // Parse Annex-B SPS/PPS into an avcC (length-prefixed) extradata block.
  StatusCode BuildExtradata(const uint8_t* data, size_t size,
                            std::vector<uint8_t>* out);

  // Convert one Annex-B packet to AVCC (4-byte lengths, SPS/PPS dropped).
  StatusCode AnnexBToAvcc(const uint8_t* data, size_t size,
                          std::vector<uint8_t>* out);

  ByteSink* sink_;
  int width_;
  int height_;
  int fps_;
  AVFormatContext* fmt_ = nullptr;
  int stream_index_ = -1;
  bool opened_ = false;
};

}  // namespace codec
}  // namespace video
