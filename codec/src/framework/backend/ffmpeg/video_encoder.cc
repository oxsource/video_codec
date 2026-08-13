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
#include "queue/queue_iface.h"

namespace video {
namespace codec {

namespace {
AVCodecID ToCodecId(VideoCodecType t) {
  return t == VideoCodecType::kHEVC ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
}
// Annex-B bitstream filter name for the chosen codec.
const char* BsfName(VideoCodecType t) {
  return t == VideoCodecType::kHEVC ? "hevc_mp4toannexb" : "h264_mp4toannexb";
}
AVPixelFormat ToAvPixFmt(PixelFormat f) {
  switch (f) {
    case PixelFormat::kI420:
      return AV_PIX_FMT_YUV420P;
    case PixelFormat::kNV12:
      return AV_PIX_FMT_NV12;
    default:
      return AV_PIX_FMT_NONE;  // RGBA not accepted by this backend
  }
}
}  // namespace

FFmpegVideoEncoder::FFmpegVideoEncoder(const VideoEncoderConfig& config)
    : config_(config) {}

FFmpegVideoEncoder::~FFmpegVideoEncoder() { Release(); }

Status FFmpegVideoEncoder::Init() {
  // Guard the entry state up front (Created or Flushed allowed) WITHOUT
  // committing the transition: the lifecycle flips to Initialized only after
  // the real FFmpeg init below succeeds, so a failed Init leaves the encoder
  // in its previous state and reusable.
  if (!lifecycle_.CanInit()) return Status::kInvalidArgument;

  if (!config_.IsValid()) return Status::kInvalidArgument;

  const AVCodec* codec = avcodec_find_encoder(ToCodecId(config_.codec));
  if (!codec) {
    VC_LOG(LogLevel::kError, "FFmpegVideoEncoder: encoder not found");
    return Status::kPlatformUnsupported;
  }
  ctx_.reset(avcodec_alloc_context3(codec));
  if (!ctx_) return Status::kEncodeFailed;

  AVPixelFormat pix = ToAvPixFmt(config_.input_format);
  if (pix == AV_PIX_FMT_NONE) return Status::kUnsupportedFormat;

  ctx_->width = config_.width;
  ctx_->height = config_.height;
  ctx_->pix_fmt = pix;
  ctx_->bit_rate = config_.bitrate;
  ctx_->time_base = AVRational{1, config_.fps > 0 ? config_.fps : 30};
  ctx_->framerate = AVRational{config_.fps > 0 ? config_.fps : 30, 1};
  ctx_->gop_size = config_.gop_size > 0 ? config_.gop_size : (config_.fps * 2);
  ctx_->max_b_frames = 0;  // simplify; keyframes carry SPS/PPS via the bsf

  if (avcodec_open2(ctx_.get(), codec, nullptr) < 0) {
    return Status::kEncodeFailed;
  }

  frame_.reset(av_frame_alloc());
  frame_->format = pix;
  frame_->width = config_.width;
  frame_->height = config_.height;
  if (av_frame_get_buffer(frame_.get(), 0) < 0) {
    return Status::kEncodeFailed;
  }

  // Annex-B bitstream filter: FFmpeg emits length-prefixed (AVCC); the bsf
  // converts to start-code (Annex-B) and prepends SPS/PPS at the first
  // keyframe.
  const AVBitStreamFilter* filter = av_bsf_get_by_name(BsfName(config_.codec));
  AVBSFContext* raw_bsf = nullptr;
  if (!filter || av_bsf_alloc(filter, &raw_bsf) < 0) {
    return Status::kEncodeFailed;
  }
  bsf_.reset(raw_bsf);

  AVCodecParameters* par = avcodec_parameters_alloc();
  avcodec_parameters_from_context(par, ctx_.get());
  // av_bsf_alloc leaves par_in/par_out NULL; allocate par_in before copying.
  bsf_->par_in = avcodec_parameters_alloc();
  if (!bsf_->par_in) {
    avcodec_parameters_free(&par);
    return Status::kEncodeFailed;
  }
  avcodec_parameters_copy(bsf_->par_in, par);
  avcodec_parameters_free(&par);
  if (av_bsf_init(bsf_.get()) < 0) {
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

Status FFmpegVideoEncoder::CopyFrame(const VideoFrame& frame) {
  if (frame.width != config_.width || frame.height != config_.height) {
    return Status::kInvalidArgument;
  }
  if (av_frame_make_writable(frame_.get()) < 0) return Status::kEncodeFailed;

  const int planes = (frame.format == PixelFormat::kNV12) ? 2 : 3;
  for (int p = 0; p < planes; ++p) {
    const uint8_t* src = frame.planes[p].data();
    uint8_t* dst = frame_->data[p];
    int src_stride =
        frame.stride[p] > 0 ? frame.stride[p] : frame_->linesize[p];
    // Chroma height is h/2 for both NV12 and I420 (the original ternary was
    // redundant).
    const int rows = (p == 0) ? frame.height : frame.height / 2;
    // Bytes per row copied: luma = width; NV12 UV = width (interleaved); I420
    // U/V = width/2.
    int copy_bytes =
        (p == 0) ? frame_->width
                 : ((frame.format == PixelFormat::kNV12) ? frame_->width
                                                         : frame_->width / 2);
    for (int y = 0; y < rows; ++y) {
      std::memcpy(dst + y * frame_->linesize[p], src + y * src_stride,
                  static_cast<size_t>(copy_bytes));
    }
  }
  frame_->pts = pts_++;
  return Status::kOk;
}

Result<VideoPacket> FFmpegVideoEncoder::Encode(const VideoFrame& frame) {
  if (lifecycle_.Encode() != Status::kOk)
    return Err<VideoPacket>(Status::kNotInitialized);
  if (CopyFrame(frame) != Status::kOk)
    return Err<VideoPacket>(Status::kInvalidArgument);

  if (avcodec_send_frame(ctx_.get(), frame_.get()) < 0)
    return Err<VideoPacket>(Status::kEncodeFailed);
  return Drain(/*drain_eof=*/false);
}

Result<VideoPacket> FFmpegVideoEncoder::Encode(const NativeBuffer&) {
  // Software FFmpeg path has no hardware surface.
  return Err<VideoPacket>(Status::kUnsupportedOperation);
}

Result<VideoPacket> FFmpegVideoEncoder::Flush() {
  if (lifecycle_.Flush() != Status::kOk)
    return Err<VideoPacket>(Status::kNotInitialized);
  if (avcodec_send_frame(ctx_.get(), nullptr) < 0) {
    // Already drained; still try to pull remaining bsf output.
  }
  Result<VideoPacket> r = Drain(/*drain_eof=*/true);
  if (sink_) sink_->Flush();
  return r;
}

Status FFmpegVideoEncoder::SetOutputSink(OutputSink* sink) {
  sink_ = sink;
  return Status::kOk;
}

Result<VideoPacket> FFmpegVideoEncoder::Drain(bool drain_eof) {
  VideoPacket out;  // pull-mode result (push mode returns an empty packet)
  for (;;) {
    int ret = avcodec_receive_packet(ctx_.get(), pkt_.get());
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
    if (ret < 0) return Err<VideoPacket>(Status::kEncodeFailed);

    // Convert this codec packet to Annex-B via the bsf.
    AVPacket* bsf_out = av_packet_alloc();
    if (!bsf_out) {
      av_packet_unref(pkt_.get());
      return Err<VideoPacket>(Status::kEncodeFailed);
    }
    if (av_bsf_send_packet(bsf_.get(), pkt_.get()) < 0) {
      av_packet_free(&bsf_out);
      av_packet_unref(pkt_.get());  // release the codec packet's ref
      return Err<VideoPacket>(Status::kEncodeFailed);
    }
    while (av_bsf_receive_packet(bsf_.get(), bsf_out) == 0) {
      VideoPacket pkt;
      pkt.data.assign(bsf_out->data, bsf_out->data + bsf_out->size);
      pkt.pts_us = bsf_out->pts * av_q2d(ctx_->time_base) * 1'000'000;
      pkt.keyframe = (bsf_out->flags & AV_PKT_FLAG_KEY) != 0;
      av_packet_unref(bsf_out);
      if (sink_) {
        // Push mode: single destination is the sink.
        if (sink_->Submit(std::move(pkt)) != Status::kOk) {
          av_packet_free(&bsf_out);
          av_packet_unref(pkt_.get());
          return Err<VideoPacket>(Status::kEncodeFailed);
        }
      } else {
        out = std::move(pkt);
      }
    }
    av_packet_free(&bsf_out);
    av_packet_unref(pkt_.get());
    // Do NOT break on drain_eof: x264 buffers frames internally; flush must
    // drain every buffered codec packet so none are lost (push mode submits
    // each; pull mode returns the last one).
  }
  return Ok(std::move(out));
}

void FFmpegVideoEncoder::Release() {
  lifecycle_.Release();
  sink_ = nullptr;  // non-owning; caller owns the sink lifetime
  bsf_.reset();
  pkt_.reset();
  frame_.reset();
  ctx_.reset();
}

}  // namespace codec
}  // namespace video
