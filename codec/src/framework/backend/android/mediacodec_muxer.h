// mediacodec_muxer.h
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

#include "mediacodec_raii.h"
#include "muxer.h"
#include "types.h"

namespace video {
namespace codec {

class ByteSink;

// Android MediaCodec (NDK AMediaMuxer) muxer backend: writes MP4. AMediaMuxer
// requires a seekable file descriptor (moov is written by seeking back at
// stop()), so this backend routes all muxer output through a seekable temp
// file and replays the whole file to the attached ByteSink at Finish()
// (research R4; contract C-030..C-035). Tracks are added lazily: the video
// track on the first keyframe (SPS/PPS -> csd-0/csd-1), the audio track on the
// first audio packet (AAC ASC -> csd-0); AMediaMuxer_start runs once both
// expected tracks exist, then buffered samples are replayed.
class MediaCodecMuxer : public Muxer {
 public:
  explicit MediaCodecMuxer(const MuxerConfig& config);
  ~MediaCodecMuxer() override;

  Status SetOutput(ByteSink* sink) override;
  Status Push(VideoPacket&& pkt) override;
  Status Push(AudioPacket&& pkt) override;
  Status Flush() override;
  Status Finish() override;
  void Release() override;

 private:
  struct PendingSample {
    size_t track;
    std::vector<uint8_t> data;
    int64_t pts_us;
    bool keyframe;
  };

  // Lazily create the temp file + AMediaMuxer (first use).
  Status EnsureMuxer();
  // Extract SPS/PPS from an Annex-B keyframe into sps_/pps_.
  Status CaptureVideoCsd(const VideoPacket& pkt);
  // Strip SPS/PPS NAL units (types 7/8) from an Annex-B access unit.
  Status StripCsd(const std::vector<uint8_t>& in, std::vector<uint8_t>* out);
  // Add a track (before start). Returns the track index or -1.
  ssize_t AddVideoTrack();
  ssize_t AddAudioTrack();
  // True once every configured track has been added (can call start).
  bool CanStart() const;
  // AMediaMuxer_start + replay of buffered samples.
  Status Start();
  // Write one sample; buffers it until the muxer is started.
  Status WriteSample(size_t track, const std::vector<uint8_t>& data, int64_t pts_us,
                     bool keyframe);
  // Stop the muxer, replay the temp file to the sink, and free resources.
  Status Finalize();

  MuxerConfig config_;
  ByteSink* sink_ = nullptr;
  android::MediaMuxerPtr muxer_;  // RAII: AMediaMuxer_delete on destruction
  std::FILE* tmp_ = nullptr;  // seekable temp file backing the fd
  int tmp_fd_ = -1;
  bool started_ = false;
  bool finished_ = false;
  ssize_t video_track_ = -1;
  ssize_t audio_track_ = -1;
  std::vector<uint8_t> sps_;
  std::vector<uint8_t> pps_;
  std::vector<uint8_t> asc_;
  std::vector<PendingSample> pending_;  // samples delivered before start()
};

}  // namespace codec
}  // namespace video
