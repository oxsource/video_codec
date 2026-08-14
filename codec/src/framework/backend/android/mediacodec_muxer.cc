// mediacodec_muxer.cc
#include "mediacodec_muxer.h"

#include <cstring>

#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>

#include "byte_sink.h"
#include "codec_factory.h"
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

ssize_t MediaCodecMuxer::AddVideoTrack() {
  android::MediaFormatPtr fmt(AMediaFormat_new());
  AMediaFormat_setString(fmt.get(), AMEDIAFORMAT_KEY_MIME, "video/avc");
  AMediaFormat_setInt32(fmt.get(), AMEDIAFORMAT_KEY_WIDTH, config_.width);
  AMediaFormat_setInt32(fmt.get(), AMEDIAFORMAT_KEY_HEIGHT, config_.height);
  AMediaFormat_setInt32(fmt.get(), AMEDIAFORMAT_KEY_FRAME_RATE, config_.fps > 0 ? config_.fps : 30);
  // The codec output format (AMediaCodec_getOutputFormat) carries csd-0/csd-1
  // as start-code-prefixed SPS/PPS on this platform; MediaMuxer builds the
  // avcC in moov from exactly this form. Match it verbatim: a synthesized
  // avcC box is rejected ("Missing codec specific data"), a bare SPS yields a
  // malformed avcC ("non-existing PPS 0 referenced").
  if (!sps_.empty()) {
    std::vector<uint8_t> csd = {0, 0, 0, 1};
    csd.insert(csd.end(), sps_.begin(), sps_.end());
    AMediaFormat_setBuffer(fmt.get(), kCsd0, csd.data(), csd.size());
  }
  if (!pps_.empty()) {
    std::vector<uint8_t> csd = {0, 0, 0, 1};
    csd.insert(csd.end(), pps_.begin(), pps_.end());
    AMediaFormat_setBuffer(fmt.get(), kCsd1, csd.data(), csd.size());
  }
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
  // AMediaMuxer_writeSampleData consumes the sample asynchronously on a
  // background writer thread (libstagefright MPEG4Writer) that joins at
  // AMediaMuxer_stop(). Keep a private copy alive until then; handing it the
  // caller's buffer (e.g. a temporary in Push()) would read freed memory.
  in_flight_.push_back(data);
  AMediaCodecBufferInfo info{};
  info.offset = 0;
  info.size = static_cast<int32_t>(in_flight_.back().size());
  info.presentationTimeUs = pts_us;
  info.flags = keyframe ? kKeyFrameFlag : 0;
  return AMediaMuxer_writeSampleData(muxer_.get(), track, in_flight_.back().data(), &info) ==
                 AMEDIA_OK
             ? Status::kOk
             : Status::kEncodeFailed;
}

Status MediaCodecMuxer::Push(VideoPacket&& pkt) {
  if (pkt.data.empty()) return Status::kOk;
  if (!sink_) return Status::kInvalidArgument;
  const Status s = EnsureMuxer();
  if (s != Status::kOk) return s;

  if (video_track_ < 0) {
    // Wait for the first keyframe: it carries SPS/PPS for the avcC csd.
    if (!pkt.keyframe) return Status::kOk;
    if (CaptureVideoCsd(pkt) != Status::kOk) return Status::kEncodeFailed;
    video_track_ = AddVideoTrack();
    if (video_track_ < 0) return Status::kEncodeFailed;
  }

  // Pass the Annex-B access unit through as-is, minus the LEADING start code.
  // libstagefright's addMultipleLengthPrefixedSamples_l reads the first NAL of
  // a sample buffer as not start-code delimited (the buffer boundary is the
  // separator) and only uses start codes to split subsequent NALs; a leading
  // start code is misread as an empty first NAL (mp4 sample framing:
  // "Invalid NAL unit size (0 > ...)").
  std::vector<uint8_t> sample = pkt.data;
  const uint8_t* d = pkt.data.data();
  const size_t n = pkt.data.size();
  if (n >= 4 && d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 1) {
    sample.assign(d + 4, d + n);
  } else if (n >= 3 && d[0] == 0 && d[1] == 0 && d[2] == 1) {
    sample.assign(d + 3, d + n);
  }
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
    // The async writer thread has joined; the sample buffers we handed it are
    // no longer read, so the in-flight copies can be released.
    in_flight_.clear();
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

// Self-registration (atlas-style); `mediacodec_muxer` carries alwayslink and
// is android-only (target_compatible_with), so this only compiles on Android.
namespace video {
namespace codec {
VIDEO_CODEC_REGISTER_MUXER(Backend::kAndroid, MediaCodecMuxer)
}  // namespace codec
}  // namespace video
