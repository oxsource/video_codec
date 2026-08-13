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

// Muxing options.
struct MuxOptions {
  // Fragmented MP4 (fMP4): write the header (ftyp + moov) upfront and emit one
  // fragment per keyframe, flushing each fragment to the sink as it completes.
  // Sequential (no seeking) — recoverable on interrupted writes and streamable
  // to network/cloud sinks. Default on.
  bool fragmented = true;
};

// Muxes encoded H.264 Annex-B packets into the MP4 container (FFmpeg's mov
// muxer). Purely a format converter: every output byte is written through a
// caller-supplied ByteSink (file, memory buffer, network stream, ...) — this
// class never touches the filesystem.
//
// The encoder emits Annex-B samples (start codes, SPS/PPS inline). This muxer
// converts them to length-prefixed (AVCC) samples, builds the avcC extradata
// from the first keyframe's SPS/PPS, and writes an MP4 (fragmented by default).
// Geometry and framerate come from the encoder config (fallbacks if SPS parsing
// yields no dimensions).
class Mp4Muxer {
 public:
  // `sink` must outlive this muxer. `fps` drives the stream time_base;
  // `width`/`height` are fallbacks for the stream header.
  Mp4Muxer(ByteSink* sink, int width = 0, int height = 0, int fps = 30,
           const MuxOptions& options = MuxOptions());
  ~Mp4Muxer();

  Mp4Muxer(const Mp4Muxer&) = delete;
  Mp4Muxer& operator=(const Mp4Muxer&) = delete;

  // Feed one encoded packet. The MP4 is opened lazily on the first keyframe
  // (it needs SPS/PPS for the avcC extradata); non-keyframes before that are
  // dropped. In fragmented mode, each completed fragment is flushed to the
  // sink (the commit point for unstable/streaming sinks).
  Status Consume(const VideoPacket& pkt);

  // Write the MP4 trailer and flush the sink. Safe to call once.
  Status Finish();

 private:
  // Open the mp4 muxer lazily on the first keyframe (needs SPS/PPS).
  Status OpenMuxer(const VideoPacket& first_keyframe);

  // Parse Annex-B SPS/PPS into an avcC (length-prefixed) extradata block.
  Status BuildExtradata(const uint8_t* data, size_t size,
                        std::vector<uint8_t>* out);

  // Convert one Annex-B packet to AVCC (4-byte lengths, SPS/PPS dropped).
  Status AnnexBToAvcc(const uint8_t* data, size_t size,
                      std::vector<uint8_t>* out);

  ByteSink* sink_;
  int width_;
  int height_;
  int fps_;
  bool fragmented_;
  AVFormatContext* fmt_ = nullptr;
  int stream_index_ = -1;
  bool opened_ = false;
};

}  // namespace codec
}  // namespace video
