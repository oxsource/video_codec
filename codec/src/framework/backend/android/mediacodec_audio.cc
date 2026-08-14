// mediacodec_audio.cc
#include "mediacodec_audio.h"

#include <cstring>

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include "mediacodec_utils.h"
#include "packet_sink.h"

namespace video {
namespace codec {

namespace {

constexpr int64_t kDequeueTimeoutUs = 10'000;

// Wait for an input buffer slot, returns its index or -1.
ssize_t DequeueInput(AMediaCodec* codec) {
  return AMediaCodec_dequeueInputBuffer(codec, kDequeueTimeoutUs);
}

}  // namespace

MediaCodecAudioEncoder::MediaCodecAudioEncoder(const AudioConfig& config) : config_(config) {}

MediaCodecAudioEncoder::~MediaCodecAudioEncoder() { Release(); }

Status MediaCodecAudioEncoder::Init() {
  // Guard the entry state WITHOUT committing: the lifecycle flips only after
  // the real MediaCodec init succeeds, so a failed Init leaves the encoder
  // reusable in its previous state.
  if (!lifecycle_.CanInit()) return Status::kInvalidArgument;
  if (!config_.IsValid()) return Status::kInvalidArgument;
  if (config_.codec != AudioCodecType::kAAC) return Status::kUnsupportedFormat;

  const char* mime = android::MimeFor(config_.codec);
  codec_.reset(AMediaCodec_createEncoderByType(mime));
  if (!codec_) return Status::kPlatformUnsupported;

  format_.reset(AMediaFormat_new());
  AMediaFormat_setString(format_.get(), AMEDIAFORMAT_KEY_MIME, mime);
  AMediaFormat_setInt32(format_.get(), AMEDIAFORMAT_KEY_SAMPLE_RATE, config_.sample_rate);
  AMediaFormat_setInt32(format_.get(), AMEDIAFORMAT_KEY_CHANNEL_COUNT, config_.channels);
  AMediaFormat_setInt32(format_.get(), AMEDIAFORMAT_KEY_BIT_RATE, config_.bitrate);
  AMediaFormat_setInt32(format_.get(), AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 16 * 1024);

  const media_status_t cfg = AMediaCodec_configure(codec_.get(), format_.get(), nullptr, nullptr,
                                                   AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
  if (cfg != AMEDIA_OK || AMediaCodec_start(codec_.get()) != AMEDIA_OK) {
    Release();
    return Status::kEncodeFailed;
  }

  // All real init succeeded — only now commit the lifecycle transition.
  if (lifecycle_.Init() != Status::kOk) {  // unreachable given the top guard
    Release();
    return Status::kInvalidArgument;
  }
  return Status::kOk;
}

Status MediaCodecAudioEncoder::QueueInput(const AudioFrame& frame) {
  if (frame.format != SampleFormat::kS16) return Status::kUnsupportedFormat;
  if (frame.sample_rate != config_.sample_rate || frame.channels != config_.channels) {
    return Status::kInvalidArgument;
  }

  const ssize_t idx = DequeueInput(codec_.get());
  if (idx < 0) return Status::kEncodeFailed;

  size_t buf_size = 0;
  uint8_t* buf = AMediaCodec_getInputBuffer(codec_.get(), static_cast<size_t>(idx), &buf_size);
  if (!buf || buf_size < frame.data.size()) {
    // MediaCodec has no releaseInputBuffer: return the dequeued slot by queueing
    // an empty buffer so it is not leaked/exhausted on repeated failures.
    AMediaCodec_queueInputBuffer(codec_.get(), static_cast<size_t>(idx), 0, 0, 0, 0);
    return Status::kEncodeFailed;
  }
  std::memcpy(buf, frame.data.data(), frame.data.size());

  // Presentation timestamp in microseconds. Prefer the caller's timestamp;
  // otherwise advance an internal clock by this frame's duration (one AAC
  // frame's worth of samples) so packets stay spaced in real time even when
  // the caller feeds untimestamped PCM (AudioFrame.timestamp_us defaults to 0).
  const int64_t samples = static_cast<int64_t>(frame.data.size()) / (config_.channels * 2);
  const int64_t pts_us = frame.timestamp_us > 0 ? frame.timestamp_us : pts_;
  pts_ += samples * 1'000'000 / config_.sample_rate;

  return AMediaCodec_queueInputBuffer(codec_.get(), static_cast<size_t>(idx), 0, frame.data.size(),
                                      static_cast<uint64_t>(pts_us), 0) == AMEDIA_OK
             ? Status::kOk
             : Status::kEncodeFailed;
}

Result<AudioPacket> MediaCodecAudioEncoder::Encode(const AudioFrame& frame) {
  if (lifecycle_.Encode() != Status::kOk) return Err<AudioPacket>(Status::kNotInitialized);
  if (QueueInput(frame) != Status::kOk) return Err<AudioPacket>(Status::kEncodeFailed);
  return Drain(/*drain_eof=*/false);
}

Result<AudioPacket> MediaCodecAudioEncoder::Flush() {
  if (lifecycle_.Flush() != Status::kOk) return Err<AudioPacket>(Status::kNotInitialized);

  // Signal end-of-stream so every buffered frame is emitted, then drain.
  const ssize_t idx = DequeueInput(codec_.get());
  if (idx >= 0) {
    AMediaCodec_queueInputBuffer(codec_.get(), static_cast<size_t>(idx), 0, 0, 0,
                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
  }
  Result<AudioPacket> r = Drain(/*drain_eof=*/true);
  // Reset for potential reuse (a MediaCodec in EOS state rejects new input).
  AMediaCodec_flush(codec_.get());
  if (sink_) sink_->Flush();
  return r;
}

Status MediaCodecAudioEncoder::SetOutputSink(PacketSink* sink) {
  sink_ = sink;
  return Status::kOk;
}

Result<AudioPacket> MediaCodecAudioEncoder::Drain(bool drain_eof) {
  AudioPacket out;  // pull-mode result (push mode returns an empty packet)
  for (;;) {
    AMediaCodecBufferInfo info{};
    const ssize_t idx = AMediaCodec_dequeueOutputBuffer(codec_.get(), &info, kDequeueTimeoutUs);
    if (idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      break;  // nothing ready
    }
    if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED ||
        idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
      continue;
    }
    if (idx < 0) return Err<AudioPacket>(Status::kEncodeFailed);

    // Defensive: a malformed buffer with a negative offset/size would wrap to
    // a huge size_t below and read out of bounds. Release and skip it.
    if (info.offset < 0 || info.size < 0) {
      AMediaCodec_releaseOutputBuffer(codec_.get(), static_cast<size_t>(idx), false);
      continue;
    }

    size_t out_size = 0;
    uint8_t* raw = AMediaCodec_getOutputBuffer(codec_.get(), static_cast<size_t>(idx), &out_size);
    const uint8_t* payload = raw + info.offset;
    const size_t payload_size = static_cast<size_t>(info.size);

    if ((info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0 && payload_size > 0) {
      // The AudioSpecificConfig — metadata for the muxer, not a sample.
      asc_.assign(payload, payload + payload_size);
      AMediaCodec_releaseOutputBuffer(codec_.get(), static_cast<size_t>(idx), false);
      continue;
    }

    if (payload_size > 0) {
      AudioPacket pkt;
      pkt.data.assign(payload, payload + payload_size);
      pkt.pts_us = info.presentationTimeUs;
      pkt.keyframe = false;  // audio is never a keyframe
      AMediaCodec_releaseOutputBuffer(codec_.get(), static_cast<size_t>(idx), false);
      if (sink_) {
        if (sink_->Push(std::move(pkt)) != Status::kOk) {
          return Err<AudioPacket>(Status::kEncodeFailed);
        }
      } else {
        out = std::move(pkt);
      }
    } else {
      AMediaCodec_releaseOutputBuffer(codec_.get(), static_cast<size_t>(idx), false);
    }

    if (drain_eof && (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
      break;  // EOS reached
    }
  }
  return Ok(std::move(out));
}

void MediaCodecAudioEncoder::Release() {
  lifecycle_.Release();
  sink_ = nullptr;  // non-owning; caller owns the sink lifetime
  if (codec_) AMediaCodec_stop(codec_.get());
  codec_.reset();  // AMediaCodec_delete via the RAII deleter
  format_.reset();
  asc_.clear();
}

}  // namespace codec
}  // namespace video
