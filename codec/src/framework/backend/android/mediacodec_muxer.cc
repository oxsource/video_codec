// mediacodec_muxer.cc
#include "mediacodec_muxer.h"

#include <cstring>

#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>

#include "byte_sink.h"
#include "mediacodec_utils.h"

namespace video {
namespace codec {

namespace {

constexpr uint32_t kKeyFrameFlag = 1;  // MediaCodec BUFFER_FLAG_KEY_FRAME
constexpr size_t kReplayBufferSize = 64 * 1024;

const char* kCsd0 = "csd-0";
const char* kCsd1 = "csd-1";

}  // namespace

MediaCodecMuxer::MediaCodecMuxer(const MuxerConfig& config) : config_(config) {}

MediaCodecMuxer::~MediaCodecMuxer() { Release(); }

Status MediaCodecMuxer::SetOutput(ByteSink* sink) {
  sink_ = sink;
  return Status::kOk;
}

Status MediaCodecMuxer::EnsureMuxer() {
  if (muxer_) return Status::kOk;
  if (!config_.IsValid()) return Status::kInvalidArgument;
  tmp_ = std::tmpfile();
  if (!tmp_) return Status::kEncodeFailed;
  tmp_fd_ = fileno(tmp_);
  muxer_.reset(AMediaMuxer_new(tmp_fd_, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4));
  if (!muxer_) {
    std::fclose(tmp_);
    tmp_ = nullptr;
    tmp_fd_ = -1;
    return Status::kEncodeFailed;
  }
  return Status::kOk;
}

Status MediaCodecMuxer::CaptureVideoCsd(const VideoPacket& pkt) {
  std::vector<std::vector<uint8_t>> units;
  if (!android::SplitAnnexB(pkt.data.data(), pkt.data.size(), &units)) {
    return Status::kEncodeFailed;
  }
  for (const auto& u : units) {
    if (u.empty()) continue;
    const int type = u[0] & 0x1F;
    if (type == 7 && sps_.empty()) sps_ = u;
    if (type == 8 && pps_.empty()) pps_ = u;
  }
  return (sps_.empty() || pps_.empty()) ? Status::kEncodeFailed : Status::kOk;
}

Status MediaCodecMuxer::StripCsd(const std::vector<uint8_t>& in, std::vector<uint8_t>* out) {
  out->clear();
  std::vector<std::vector<uint8_t>> units;
  if (!android::SplitAnnexB(in.data(), in.size(), &units)) {
    *out = in;  // not Annex-B; pass through
    return Status::kOk;
  }
  for (const auto& u : units) {
    if (u.empty()) continue;
    const int type = u[0] & 0x1F;
    if (type == 7 || type == 8) continue;  // SPS/PPS live in csd-0/csd-1 only
    out->insert(out->end(), {0, 0, 0, 1});
    out->insert(out->end(), u.begin(), u.end());
  }
  return out->empty() ? Status::kEncodeFailed : Status::kOk;
}

ssize_t MediaCodecMuxer::AddVideoTrack() {
  android::MediaFormatPtr fmt(AMediaFormat_new());
  AMediaFormat_setString(fmt.get(), AMEDIAFORMAT_KEY_MIME, "video/avc");
  AMediaFormat_setInt32(fmt.get(), AMEDIAFORMAT_KEY_WIDTH, config_.width);
  AMediaFormat_setInt32(fmt.get(), AMEDIAFORMAT_KEY_HEIGHT, config_.height);
  AMediaFormat_setInt32(fmt.get(), AMEDIAFORMAT_KEY_FRAME_RATE, config_.fps > 0 ? config_.fps : 30);
  if (!sps_.empty()) AMediaFormat_setBuffer(fmt.get(), kCsd0, sps_.data(), sps_.size());
  if (!pps_.empty()) AMediaFormat_setBuffer(fmt.get(), kCsd1, pps_.data(), pps_.size());
  return AMediaMuxer_addTrack(muxer_.get(), fmt.get());
}

ssize_t MediaCodecMuxer::AddAudioTrack() {
  android::MediaFormatPtr fmt(AMediaFormat_new());
  AMediaFormat_setString(fmt.get(), AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
  AMediaFormat_setInt32(fmt.get(), AMEDIAFORMAT_KEY_SAMPLE_RATE, config_.sample_rate);
  AMediaFormat_setInt32(fmt.get(), AMEDIAFORMAT_KEY_CHANNEL_COUNT, config_.channels);
  if (!asc_.empty()) AMediaFormat_setBuffer(fmt.get(), kCsd0, asc_.data(), asc_.size());
  return AMediaMuxer_addTrack(muxer_.get(), fmt.get());
}

bool MediaCodecMuxer::CanStart() const {
  if (video_track_ < 0) return false;
  if (config_.audio_codec != AudioCodecType::kNone && audio_track_ < 0) return false;
  return true;
}

Status MediaCodecMuxer::Start() {
  if (started_) return Status::kOk;
  if (AMediaMuxer_start(muxer_.get()) != AMEDIA_OK) return Status::kEncodeFailed;
  started_ = true;
  for (const PendingSample& s : pending_) {
    if (WriteSample(s.track, s.data, s.pts_us, s.keyframe) != Status::kOk) {
      return Status::kEncodeFailed;
    }
  }
  pending_.clear();
  return Status::kOk;
}

Status MediaCodecMuxer::WriteSample(size_t track, const std::vector<uint8_t>& data, int64_t pts_us,
                                    bool keyframe) {
  if (!started_) {
    if (CanStart()) {
      const Status s = Start();
      if (s != Status::kOk) return s;
    }
    if (!started_) {
      pending_.push_back(PendingSample{track, data, pts_us, keyframe});
      return Status::kOk;
    }
  }
  AMediaCodecBufferInfo info{};
  info.offset = 0;
  info.size = static_cast<int32_t>(data.size());
  info.presentationTimeUs = pts_us;
  info.flags = keyframe ? kKeyFrameFlag : 0;
  return AMediaMuxer_writeSampleData(muxer_.get(), track, data.data(), &info) == AMEDIA_OK
             ? Status::kOk
             : Status::kEncodeFailed;
}

Status MediaCodecMuxer::Push(VideoPacket&& pkt) {
  if (pkt.data.empty()) return Status::kOk;
  if (!sink_) return Status::kInvalidArgument;
  const Status s = EnsureMuxer();
  if (s != Status::kOk) return s;

  if (video_track_ < 0) {
    // Wait for the first keyframe: it carries SPS/PPS for csd-0/csd-1.
    if (!pkt.keyframe) return Status::kOk;
    if (CaptureVideoCsd(pkt) != Status::kOk) return Status::kEncodeFailed;
    video_track_ = AddVideoTrack();
    if (video_track_ < 0) return Status::kEncodeFailed;
  }

  std::vector<uint8_t> sample;
  if (StripCsd(pkt.data, &sample) != Status::kOk) return Status::kEncodeFailed;
  return WriteSample(static_cast<size_t>(video_track_), sample, pkt.pts_us, pkt.keyframe);
}

Status MediaCodecMuxer::Push(AudioPacket&& pkt) {
  if (pkt.data.empty()) return Status::kOk;
  if (!sink_) return Status::kInvalidArgument;
  if (config_.audio_codec != AudioCodecType::kAAC) return Status::kUnsupportedOperation;
  const Status s = EnsureMuxer();
  if (s != Status::kOk) return s;

  if (audio_track_ < 0) {
    if (!android::BuildAudioSpecificConfig(config_.sample_rate, config_.channels, &asc_)) {
      return Status::kEncodeFailed;
    }
    audio_track_ = AddAudioTrack();
    if (audio_track_ < 0) return Status::kEncodeFailed;
  }
  return WriteSample(static_cast<size_t>(audio_track_), pkt.data, pkt.pts_us, false);
}

Status MediaCodecMuxer::Flush() { return Status::kOk; }

Status MediaCodecMuxer::Finalize() {
  Status result = Status::kOk;
  if (muxer_ && started_) {
    if (AMediaMuxer_stop(muxer_.get()) != AMEDIA_OK) {
      // The file is incomplete; skip the replay but still release everything
      // below (no early return — Finalize must not leak tmp_/muxer_).
      result = Status::kEncodeFailed;
    }
  }
  if (result == Status::kOk && tmp_ && sink_) {
    // Replay the whole file (written by AMediaMuxer through the fd) to the sink.
    if (std::fflush(tmp_) != 0) result = Status::kEncodeFailed;
    if (result == Status::kOk && std::fseek(tmp_, 0, SEEK_SET) != 0) {
      result = Status::kEncodeFailed;
    }
    std::vector<uint8_t> buf(kReplayBufferSize);
    while (result == Status::kOk) {
      const size_t n = std::fread(buf.data(), 1, buf.size(), tmp_);
      if (n > 0 && !sink_->Write(buf.data(), n)) {
        result = Status::kEncodeFailed;
      }
      if (n < buf.size()) break;
    }
    if (result == Status::kOk && std::ferror(tmp_)) result = Status::kEncodeFailed;
    if (result == Status::kOk && !sink_->Flush()) result = Status::kEncodeFailed;
  }
  muxer_.reset();  // AMediaMuxer_delete via the RAII deleter
  if (tmp_) {
    std::fclose(tmp_);
    tmp_ = nullptr;
    tmp_fd_ = -1;
  }
  started_ = false;
  pending_.clear();
  return result;
}

Status MediaCodecMuxer::Finish() {
  if (finished_) return Status::kOk;
  const Status s = Finalize();
  finished_ = true;
  return s;
}

void MediaCodecMuxer::Release() {
  if (!finished_) Finalize();
  finished_ = true;
  sink_ = nullptr;  // non-owning; caller owns the sink lifetime
}

}  // namespace codec
}  // namespace video
