// ffmpeg_muxer.h
#pragma once

#include <memory>

#include "muxer.h"
#include "types.h"

struct AVFormatContext;

namespace video {
namespace codec {

// FFmpeg (libavformat) muxer backend. Muxes encoded H.264 Annex-B packets
// into a fragmented MP4 container, writing every byte through the io::ByteSink
// attached via SetOutput(). Implements the generic api Muxer interface.
//
// The container opens lazily on the first keyframe (it needs SPS/PPS for the
// avcC extradata); earlier non-keyframes are dropped. The first keyframe
// produces the header + first fragment as ONE output unit so the pull path
// (single Encode() delivery) also yields a usable MP4 prefix.
class FFmpegMuxer : public Muxer {
 public:
  explicit FFmpegMuxer(const MuxerConfig& config);
  ~FFmpegMuxer() override;

  FFmpegMuxer(const FFmpegMuxer&) = delete;
  FFmpegMuxer& operator=(const FFmpegMuxer&) = delete;

  Status SetOutput(ByteSink* sink) override;
  Status Push(VideoPacket&& pkt) override;
  Status Flush() override;
  Status Finish() override;
  void Release() override;

 private:
  // Open the mp4 muxer lazily on the first keyframe (needs SPS/PPS).
  Status OpenMuxer(const VideoPacket& first_keyframe);

  // Parse Annex-B SPS/PPS into an avcC (length-prefixed) extradata block.
  Status BuildExtradata(const uint8_t* data, size_t size, std::vector<uint8_t>* out);

  // Convert one Annex-B packet to AVCC (4-byte lengths, SPS/PPS dropped).
  Status AnnexBToAvcc(const uint8_t* data, size_t size, std::vector<uint8_t>* out);

  // avio write callback: forward muxer output bytes to the attached sink.
  static int SinkWrite(void* opaque, uint8_t* buf, int size);

  MuxerConfig config_;
  ByteSink* sink_ = nullptr;  // non-owning; must outlive this muxer
  bool opened_ = false;
  AVFormatContext* fmt_ = nullptr;
  int stream_index_ = -1;
};

}  // namespace codec
}  // namespace video
