// video_encoder.cc
#include "backend/ffmpeg/video_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

#include "core/log_slot.h"

namespace video {
namespace codec {

namespace {
AVCodecID ToCodecId(VideoCodecType t) {
  return t == VideoCodecType::kHEVC ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
}
// Annex-B bitstream filter name for the chosen codec.
const char* BsfName(VideoCodecType t) {
  return t == VideoCodecType::kHEVC ? "hevc_mp4toannexb"
                                    : "h264_mp4toannexb";
}
AVPixelFormat ToAvPixFmt(PixelFormat f) {
  switch (f) {
    case PixelFormat::kI420: return AV_PIX_FMT_YUV420P;
    case PixelFormat::kNV12: return AV_PIX_FMT_NV12;
    default: return AV_PIX_FMT_NONE;  // RGBA not accepted by this backend
  }
}
}  // namespace

FFmpegVideoEncoder::FFmpegVideoEncoder(const VideoEncoderConfig& config)
    : config_(config) {}

FFmpegVideoEncoder::~FFmpegVideoEncoder() { Release(); }

StatusCode FFmpegVideoEncoder::Init() {
  if (lifecycle_.Init() != StatusCode::kOk) return StatusCode::kInvalidArgument;

  if (config_.width <= 0 || config_.height <= 0) return StatusCode::kInvalidArgument;

  const AVCodec* codec = avcodec_find_encoder(ToCodecId(config_.codec));
  if (!codec) {
    VC_LOG(LogLevel::kError, "FFmpegVideoEncoder: encoder not found");
    return StatusCode::kPlatformUnsupported;
  }
  ctx_ = avcodec_alloc_context3(codec);
  if (!ctx_) return StatusCode::kEncodeFailed;

  AVPixelFormat pix = ToAvPixFmt(config_.input_format);
  if (pix == AV_PIX_FMT_NONE) {
    avcodec_free_context(&ctx_);
    return StatusCode::kUnsupportedFormat;
  }

  ctx_->width = config_.width;
  ctx_->height = config_.height;
  ctx_->pix_fmt = pix;
  ctx_->bit_rate = config_.bitrate;
  ctx_->time_base = AVRational{1, config_.fps > 0 ? config_.fps : 30};
  ctx_->framerate = AVRational{config_.fps > 0 ? config_.fps : 30, 1};
  ctx_->gop_size = config_.gop_size > 0 ? config_.gop_size : (config_.fps * 2);
  ctx_->max_b_frames = 0;  // simplify; keyframes carry SPS/PPS via the bsf

  if (avcodec_open2(ctx_, codec, nullptr) < 0) {
    avcodec_free_context(&ctx_);
    return StatusCode::kEncodeFailed;
  }

  frame_ = av_frame_alloc();
  frame_->format = pix;
  frame_->width = config_.width;
  frame_->height = config_.height;
  if (av_frame_get_buffer(frame_, 0) < 0) {
    av_frame_free(&frame_);
    avcodec_free_context(&ctx_);
    return StatusCode::kEncodeFailed;
  }

  // Annex-B bitstream filter: FFmpeg emits length-prefixed (AVCC); the bsf
  // converts to start-code (Annex-B) and prepends SPS/PPS at the first keyframe.
  const AVBitStreamFilter* filter = av_bsf_get_by_name(BsfName(config_.codec));
  if (!filter || av_bsf_alloc(filter, &bsf_) < 0) {
    av_frame_free(&frame_);
    avcodec_free_context(&ctx_);
    return StatusCode::kEncodeFailed;
  }
  AVCodecParameters* par = avcodec_parameters_alloc();
  avcodec_parameters_from_context(par, ctx_);
  avcodec_parameters_copy(bsf_->par_in, par);
  avcodec_parameters_free(&par);
  if (av_bsf_init(bsf_) < 0) {
    av_bsf_free(&bsf_);
    av_frame_free(&frame_);
    avcodec_free_context(&ctx_);
    return StatusCode::kEncodeFailed;
  }

  pkt_ = av_packet_alloc();
  initialized_ = true;
  return StatusCode::kOk;
}

StatusCode FFmpegVideoEncoder::CopyFrame(const VideoFrame& frame) {
  if (frame.width != config_.width || frame.height != config_.height) {
    return StatusCode::kInvalidArgument;
  }
  if (av_frame_make_writable(frame_) < 0) return StatusCode::kEncodeFailed;

  const int planes = (frame.format == PixelFormat::kNV12) ? 2 : 3;
  for (int p = 0; p < planes; ++p) {
    const uint8_t* src = frame.planes[p].data();
    uint8_t* dst = frame_->data[p];
    int src_stride = frame.stride[p] > 0 ? frame.stride[p]
                                        : frame_->linesize[p];
    int rows = (p == 0) ? frame.height
                         : (frame.format == PixelFormat::kNV12 ? frame.height / 2
                                                              : frame.height / 2);
    for (int y = 0; y < rows; ++y) {
      std::memcpy(dst + y * frame_->linesize[p], src + y * src_stride,
                  static_cast<size_t>(frame_->width *
                                      (p == 0 ? 1 : (frame.format == PixelFormat::kNV12 ? 2 : 1))));
    }
  }
  frame_->pts = pts_++;
  return StatusCode::kOk;
}

Result<EncodedPacket> FFmpegVideoEncoder::Encode(const VideoFrame& frame) {
  if (lifecycle_.Encode() != StatusCode::kOk) return Err<EncodedPacket>(StatusCode::kNotInitialized);
  if (CopyFrame(frame) != StatusCode::kOk) return Err<EncodedPacket>(StatusCode::kInvalidArgument);

  if (avcodec_send_frame(ctx_, frame_) < 0) return Err<EncodedPacket>(StatusCode::kEncodeFailed);
  return Drain(/*drain_eof=*/false);
}

Result<EncodedPacket> FFmpegVideoEncoder::Encode(const NativeBuffer&) {
  // Software FFmpeg path has no hardware surface.
  return Err<EncodedPacket>(StatusCode::kUnsupportedOperation);
}

Result<EncodedPacket> FFmpegVideoEncoder::Flush() {
  if (lifecycle_.Flush() != StatusCode::kOk) return Err<EncodedPacket>(StatusCode::kNotInitialized);
  if (avcodec_send_frame(ctx_, nullptr) < 0) {
    // Already drained; still try to pull remaining bsf output.
  }
  return Drain(/*drain_eof=*/true);
}

Result<EncodedPacket> FFmpegVideoEncoder::Drain(bool drain_eof) {
  EncodedPacket out;
  for (;;) {
    int ret = avcodec_receive_packet(ctx_, pkt_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
    if (ret < 0) return Err<EncodedPacket>(StatusCode::kEncodeFailed);

    // Convert this codec packet to Annex-B via the bsf.
    AVPacket* bsf_out = av_packet_alloc();
    if (av_bsf_send_packet(bsf_, pkt_) < 0) {
      av_packet_free(&bsf_out);
      return Err<EncodedPacket>(StatusCode::kEncodeFailed);
    }
    while (av_bsf_receive_packet(bsf_, bsf_out) == 0) {
      out.data.assign(bsf_out->data, bsf_out->data + bsf_out->size);
      out.pts_us = bsf_out->pts * av_q2d(ctx_->time_base) * 1'000'000;
      out.keyframe = (bsf_out->flags & AV_PKT_FLAG_KEY) != 0;
      av_packet_unref(bsf_out);
    }
    av_packet_free(&bsf_out);
    av_packet_unref(pkt_);
    if (drain_eof) break;  // return the last packet produced during flush
  }
  return Ok(std::move(out));
}

void FFmpegVideoEncoder::Release() {
  lifecycle_.Release();
  if (bsf_) {
    av_bsf_free(&bsf_);
    bsf_ = nullptr;
  }
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
  initialized_ = false;
}

}  // namespace codec
}  // namespace video
