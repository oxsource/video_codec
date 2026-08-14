// ffmpeg_muxer.h
#pragma once

#include <memory>
#include <vector>

#include "muxer.h"
#include "types.h"

struct AVFormatContext;

namespace video {
namespace codec {

// FFmpeg (libavformat) muxer backend. Muxes encoded H.264 Annex-B packets
// (and, when MuxerConfig requests an audio track, raw AAC-LC access units)
// into a fragmented MP4 container, writing every byte through the io::ByteSink
// attached via SetOutput(). Implements the generic api Muxer interface.
//
// The container opens lazily on the first keyframe (it needs SPS/PPS for the
// avcC extradata); earlier non-keyframes are dropped. The first keyframe
// produces the header + first fragment as ONE output unit so the pull path
// (single Encode() delivery) also yields a usable MP4 prefix. Audio packets
// arriving before the open are buffered and replayed once the container is
// open (video always precedes open in the Await drain order, so this is a
// robustness fallback).
class FFmpegMuxer : public Muxer {
 public:
  explicit FFmpegMuxer(const MuxerConfig& config);
  ~FFmpegMuxer() override;

  FFmpegMuxer(const FFmpegMuxer&) = delete;
  FFmpegMuxer& operator=(const FFmpegMuxer&) = delete;

  Status SetOutput(ByteSink* sink) override;
  Status Push(VideoPacket&& pkt) override;
  Status Push(AudioPacket&& pkt) override;
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

  // Build the 2-byte AudioSpecificConfig (AAC-LC, ISO 14496-3) from sample
  // rate + channel count. Rejects rates/ layouts the mov muxer cannot carry.
  Status BuildAudioSpecificConfig(int sample_rate, int channels, std::vector<uint8_t>* out);

  // Write one audio access unit to the audio stream (used for both the direct
  // push path and the replay of packets buffered before OpenMuxer).
  Status WriteAudioPacket(const AudioPacket& pkt);

  // avio write callback: forward muxer output bytes to the attached sink.
  static int SinkWrite(void* opaque, uint8_t* buf, int size);

  MuxerConfig config_;
  ByteSink* sink_ = nullptr;  // non-owning; must outlive this muxer
  bool opened_ = false;
  AVFormatContext* fmt_ = nullptr;
  int stream_index_ = -1;  // video stream
  int audio_stream_index_ = -1;  // audio stream (>= 0 when config requests one)
  std::vector<AudioPacket> pending_audio_;  // audio buffered before open
};

}  // namespace codec
}  // namespace video
