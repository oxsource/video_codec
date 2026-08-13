// audio_encoder.cc
#include "backend/ffmpeg/audio_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <cmath>

#include "queue/queue_iface.h"

namespace video {
namespace codec {

FFmpegAudioEncoder::FFmpegAudioEncoder(const AudioEncoderConfig& config)
    : config_(config) {}

FFmpegAudioEncoder::~FFmpegAudioEncoder() { Release(); }

Status FFmpegAudioEncoder::Init() {
  // Guard the entry state up front (Created or Flushed allowed) WITHOUT
  // committing the transition: the lifecycle flips to Initialized only after
  // the real FFmpeg init below succeeds, so a failed Init leaves the encoder
  // in its previous state and reusable.
  if (!lifecycle_.CanInit()) return Status::kInvalidArgument;
  if (!config_.IsValid()) return Status::kInvalidArgument;

  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (!codec) return Status::kPlatformUnsupported;
  ctx_.reset(avcodec_alloc_context3(codec));
  if (!ctx_) return Status::kEncodeFailed;

  ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
  ctx_->sample_rate = config_.sample_rate;
  ctx_->channels = config_.channels;
  ctx_->channel_layout = av_get_default_channel_layout(config_.channels);
  ctx_->bit_rate = config_.bitrate;

  if (avcodec_open2(ctx_.get(), codec, nullptr) < 0) {
    return Status::kEncodeFailed;
  }

  frame_.reset(av_frame_alloc());
  frame_->format = AV_SAMPLE_FMT_FLTP;
  frame_->sample_rate = config_.sample_rate;
  frame_->channels = config_.channels;
  frame_->nb_samples = ctx_->frame_size > 0 ? ctx_->frame_size : 1024;
  if (av_frame_get_buffer(frame_.get(), 0) < 0) {
    return Status::kEncodeFailed;
  }

  pkt_.reset(av_packet_alloc());

  // All real init succeeded — only now commit the lifecycle transition.
  if (lifecycle_.Init() != Status::kOk) {  // unreachable given the top guard
    Release();
    return Status::kInvalidArgument;
  }
  return Status::kOk;
}

Result<AudioPacket> FFmpegAudioEncoder::Encode(const AudioFrame& frame) {
  if (lifecycle_.Encode() != Status::kOk)
    return Err<AudioPacket>(Status::kNotInitialized);
  if (frame.data.empty()) return Err<AudioPacket>(Status::kInvalidArgument);

  const int channels = config_.channels;
  const int samples = frame_->nb_samples;
  const int in_samples =
      static_cast<int>(frame.data.size()) / (channels * 2);  // S16
  const int16_t* src = reinterpret_cast<const int16_t*>(frame.data.data());

  if (av_frame_make_writable(frame_.get()) < 0)
    return Err<AudioPacket>(Status::kEncodeFailed);
  for (int c = 0; c < channels; ++c) {
    float* dst = reinterpret_cast<float*>(frame_->data[c]);
    for (int i = 0; i < samples; ++i) {
      // Pad with silence if the input is shorter than one encoder frame.
      int16_t s = (i < in_samples) ? src[i * channels + c] : 0;
      dst[i] = static_cast<float>(s) / 32768.0f;
    }
  }
  frame_->pts = pts_++;
  // Advance the consumer offset by the actual samples consumed.
  (void)in_samples;

  if (avcodec_send_frame(ctx_.get(), frame_.get()) < 0)
    return Err<AudioPacket>(Status::kEncodeFailed);
  return Drain(/*drain_eof=*/false);
}

Result<AudioPacket> FFmpegAudioEncoder::Flush() {
  if (lifecycle_.Flush() != Status::kOk)
    return Err<AudioPacket>(Status::kNotInitialized);
  avcodec_send_frame(ctx_.get(), nullptr);
  Result<AudioPacket> r = Drain(/*drain_eof=*/true);
  if (sink_) sink_->Flush();
  return r;
}

Status FFmpegAudioEncoder::SetOutputSink(PacketSink* sink) {
  sink_ = sink;
  return Status::kOk;
}

Result<AudioPacket> FFmpegAudioEncoder::Drain(bool drain_eof) {
  AudioPacket out;  // pull-mode result (push mode returns an empty packet)
  for (;;) {
    int ret = avcodec_receive_packet(ctx_.get(), pkt_.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
    if (ret < 0) return Err<AudioPacket>(Status::kEncodeFailed);
    AudioPacket pkt;
    pkt.data.assign(pkt_->data, pkt_->data + pkt_->size);
    pkt.pts_us = pkt_->pts * av_q2d(ctx_->time_base) * 1'000'000;
    pkt.keyframe = false;
    av_packet_unref(pkt_.get());
    if (sink_) {
      // Push mode: single destination is the sink.
      if (sink_->Consume(std::move(pkt)) != Status::kOk) {
        return Err<AudioPacket>(Status::kEncodeFailed);
      }
    } else {
      out = std::move(pkt);
    }
    // Do NOT break on drain_eof: AAC buffers frames; flush must drain every
    // buffered codec packet so none are lost in push mode.
  }
  return Ok(std::move(out));
}

void FFmpegAudioEncoder::Release() {
  lifecycle_.Release();
  sink_ = nullptr;  // non-owning; caller owns the sink lifetime
  pkt_.reset();
  frame_.reset();
  ctx_.reset();
}

}  // namespace codec
}  // namespace video
