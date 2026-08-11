// ffmpeg_spike.cc
//
// Minimal spike validating the FFmpeg encode integration end-to-end:
// allocate an AVCodecContext for libx264, encode a synthetic NV12 frame
// sequence, and write Annex-B H.264 bytes to ffmpeg_spike.h264.
//
// This proves the highest-risk dependency (FFmpeg libavcodec) compiles, links,
// and produces a valid bitstream before any real encoder code is written.

#include <cstdio>
#include <cstdlib>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavutil/frame.h>
}

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr int kNumFrames = 30;

// Fill an NV12 frame with a simple moving gradient so the encoder has real
// signal to compress (deterministic, no external assets needed).
void FillFrame(AVFrame* frame, int frame_index) {
  // Y plane
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      frame->data[0][y * frame->linesize[0] + x] =
          static_cast<uint8_t>(x + y + frame_index * 3);
    }
  }
  // UV (NV12 interleaved) plane
  for (int y = 0; y < kHeight / 2; ++y) {
    for (int x = 0; x < kWidth / 2; ++x) {
      frame->data[1][y * frame->linesize[1] + 2 * x] =
          static_cast<uint8_t>(128 + frame_index * 2);
      frame->data[1][y * frame->linesize[1] + 2 * x + 1] =
          static_cast<uint8_t>(64 + frame_index * 5);
    }
  }
}

// Encode one frame (or flush with nullptr) and write every emitted packet.
int EncodeAndWrite(AVCodecContext* enc, AVFrame* frame, AVPacket* pkt, FILE* out) {
  int ret = avcodec_send_frame(enc, frame);
  if (ret < 0) return ret;
  while (ret >= 0) {
    ret = avcodec_receive_packet(enc, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
    if (ret < 0) return ret;
    if (fwrite(pkt->data, 1, pkt->size, out) != static_cast<size_t>(pkt->size)) {
      return AVERROR_UNKNOWN;
    }
    av_packet_unref(pkt);
  }
  return 0;
}

}  // namespace

int main() {
  const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
  if (!codec) {
    std::fprintf(stderr, "spike: libx264 encoder not found in FFmpeg build\n");
    return 1;
  }

  AVCodecContext* enc = avcodec_alloc_context3(codec);
  if (!enc) {
    std::fprintf(stderr, "spike: failed to allocate codec context\n");
    return 1;
  }
  enc->codec_type = AVMEDIA_TYPE_VIDEO;
  enc->codec_id = AV_CODEC_ID_H264;
  enc->pix_fmt = AV_PIX_FMT_NV12;
  enc->width = kWidth;
  enc->height = kHeight;
  enc->bit_rate = 400000;
  enc->time_base = {1, 30};
  enc->framerate = {30, 1};
  enc->gop_size = 15;
  enc->max_b_frames = 0;
  // No AV_CODEC_FLAG_GLOBAL_HEADER -> SPS/PPS emitted in-band as Annex-B.

  if (avcodec_open2(enc, codec, nullptr) < 0) {
    std::fprintf(stderr, "spike: failed to open libx264 codec\n");
    avcodec_free_context(&enc);
    return 1;
  }

  AVFrame* frame = av_frame_alloc();
  frame->format = enc->pix_fmt;
  frame->width = enc->width;
  frame->height = enc->height;
  if (av_frame_get_buffer(frame, 0) < 0) {
    std::fprintf(stderr, "spike: failed to allocate frame buffer\n");
    av_frame_free(&frame);
    avcodec_free_context(&enc);
    return 1;
  }

  FILE* out = std::fopen("ffmpeg_spike.h264", "wb");
  if (!out) {
    std::fprintf(stderr, "spike: cannot open output file\n");
    av_frame_free(&frame);
    avcodec_free_context(&enc);
    return 1;
  }

  AVPacket* pkt = av_packet_alloc();
  int failed = 0;
  for (int i = 0; i < kNumFrames && !failed; ++i) {
    if (av_frame_make_writable(frame) < 0) {
      failed = 1;
      break;
    }
    FillFrame(frame, i);
    frame->pts = i;
    if (EncodeAndWrite(enc, frame, pkt, out) < 0) {
      std::fprintf(stderr, "spike: encode error at frame %d\n", i);
      failed = 1;
    }
  }
  if (!failed) EncodeAndWrite(enc, nullptr, pkt, out);  // flush

  const long bytes = std::ftell(out);
  std::fclose(out);
  av_packet_free(&pkt);
  av_frame_free(&frame);
  avcodec_free_context(&enc);

  if (failed) return 1;
  std::fprintf(stderr, "spike: wrote %ld bytes to ffmpeg_spike.h264\n", bytes);
  return bytes > 0 ? 0 : 1;
}
