// audio_encoder.cc
#include "backend/ffmpeg/audio_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <cmath>

namespace video {
namespace codec {

FFmpegAudioEncoder::FFmpegAudioEncoder(const AudioEncoderConfig& config)
    : config_(config) {}

FFmpegAudioEncoder::~FFmpegAudioEncoder() { Release(); }

StatusCode FFmpegAudioEncoder::Init() {
  if (lifecycle_.Init() != StatusCode::kOk) return StatusCode::kInvalidArgument;
  if (config_.sample_rate <= 0 || config_.channels <= 0) {
    return StatusCode::kInvalidArgument;
  }

  const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
  if (!codec) return StatusCode::kPlatformUnsupported;
  ctx_ = avcodec_alloc_context3(codec);
  if (!ctx_) return StatusCode::kEncodeFailed;

  ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
  ctx_->sample_rate = config_.sample_rate;
  ctx_->channels = config_.channels;
  ctx_->channel_layout = av_get_default_channel_layout(config_.channels);
  ctx_->bit_rate = config_.bitrate;

  if (avcodec_open2(ctx_, codec, nullptr) < 0) {
    avcodec_free_context(&ctx_);
    return StatusCode::kEncodeFailed;
  }

  frame_ = av_frame_alloc();
  frame_->format = AV_SAMPLE_FMT_FLTP;
  frame_->sample_rate = config_.sample_rate;
  frame_->channels = config_.channels;
  frame_->nb_samples = ctx_->frame_size > 0 ? ctx_->frame_size : 1024;
  if (av_frame_get_buffer(frame_, 0) < 0) {
    av_frame_free(&frame_);
    avcodec_free_context(&ctx_);
    return StatusCode::kEncodeFailed;
  }

  pkt_ = av_packet_alloc();
  return StatusCode::kOk;
}

Result<AudioPacket> FFmpegAudioEncoder::Encode(const AudioFrame& frame) {
  if (lifecycle_.Encode() != StatusCode::kOk) return Err<AudioPacket>(StatusCode::kNotInitialized);
  if (frame.data.empty()) return Err<AudioPacket>(StatusCode::kInvalidArgument);

  const int channels = config_.channels;
  const int samples = frame_->nb_samples;
  const int in_samples = static_cast<int>(frame.data.size()) / (channels * 2);  // S16
  const int16_t* src = reinterpret_cast<const int16_t*>(frame.data.data());

  if (av_frame_make_writable(frame_) < 0) return Err<AudioPacket>(StatusCode::kEncodeFailed);
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

  if (avcodec_send_frame(ctx_, frame_) < 0) return Err<AudioPacket>(StatusCode::kEncodeFailed);
  return Drain(/*drain_eof=*/false);
}

Result<AudioPacket> FFmpegAudioEncoder::Flush() {
  if (lifecycle_.Flush() != StatusCode::kOk) return Err<AudioPacket>(StatusCode::kNotInitialized);
  avcodec_send_frame(ctx_, nullptr);
  return Drain(/*drain_eof=*/true);
}

Result<AudioPacket> FFmpegAudioEncoder::Drain(bool drain_eof) {
  AudioPacket out;
  for (;;) {
    int ret = avcodec_receive_packet(ctx_, pkt_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
    if (ret < 0) return Err<AudioPacket>(StatusCode::kEncodeFailed);
    out.data.assign(pkt_->data, pkt_->data + pkt_->size);
    out.pts_us = pkt_->pts * av_q2d(ctx_->time_base) * 1'000'000;
    out.keyframe = false;
    av_packet_unref(pkt_);
    if (drain_eof) break;
  }
  return Ok(std::move(out));
}

void FFmpegAudioEncoder::Release() {
  lifecycle_.Release();
  if (pkt_) {
    av_packet_free(&pkt_);
    pkt_ = nullptr;
  }
  if (frame_) {
    av_frame_free(&frame_);
    frame_ = nullptr;
  }
  if (ctx_) {
    avcodec_free_context(&ctx_);
    ctx_ = nullptr;
  }
}

}  // namespace codec
}  // namespace video
