// ffmpeg_muxer.cc
#include "backend/ffmpeg/ffmpeg_muxer.h"

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/mem.h>
}

#include "io/byte_sink.h"

namespace video {
namespace codec {

namespace {

constexpr int kIoBufferSize = 64 * 1024;

// H.264 NAL unit type (low 5 bits of the NAL header byte).
int NalType(uint8_t b) { return b & 0x1F; }

// Detect an Annex-B start code at `p`; returns its length (3 or 4) via `*len`.
bool IsStartCode(const uint8_t* p, const uint8_t* end, int* len) {
  if (end - p >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
    *len = 4;
    return true;
  }
  if (end - p >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1) {
    *len = 3;
    return true;
  }
  return false;
}

void AppendLenPrefixed(std::vector<uint8_t>* out, const uint8_t* nal,
                       size_t nalen) {
  out->push_back(static_cast<uint8_t>(nalen >> 24));
  out->push_back(static_cast<uint8_t>(nalen >> 16));
  out->push_back(static_cast<uint8_t>(nalen >> 8));
  out->push_back(static_cast<uint8_t>(nalen));
  out->insert(out->end(), nal, nal + nalen);
}

}  // namespace

int FFmpegMuxer::SinkWrite(void* opaque, uint8_t* buf, int size) {
  auto* muxer = static_cast<FFmpegMuxer*>(opaque);
  return muxer->sink_ && muxer->sink_->Write(buf, static_cast<size_t>(size))
             ? size
             : AVERROR(EIO);
}

FFmpegMuxer::FFmpegMuxer(const MuxerConfig& config) : config_(config) {}

FFmpegMuxer::~FFmpegMuxer() {
  // Safety net: write the trailer if the caller never called Finish().
  if (opened_) {
    av_write_trailer(fmt_);
    if (fmt_->pb) {
      avio_flush(fmt_->pb);
      avio_context_free(&fmt_->pb);
    }
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    opened_ = false;
    if (sink_) sink_->Flush();
  }
}

Status FFmpegMuxer::SetOutput(ByteSink* sink) {
  sink_ = sink;
  return Status::kOk;
}

Status FFmpegMuxer::BuildExtradata(const uint8_t* data, size_t size,
                                   std::vector<uint8_t>* out) {
  const uint8_t* p = data;
  const uint8_t* end = data + size;
  std::vector<uint8_t> sps, pps;

  while (p < end && (sps.empty() || pps.empty())) {
    int sc;
    if (!IsStartCode(p, end, &sc)) {
      ++p;
      continue;
    }
    p += sc;
    const uint8_t* nal = p;
    while (p < end && !IsStartCode(p, end, &sc)) ++p;
    const size_t nalen = static_cast<size_t>(p - nal);
    if (nalen == 0) continue;
    const int type = NalType(nal[0]);
    // SPS/PPS need at least NAL header + profile + constraint + level; skip
    // truncated units so the avcC builder never reads sps[1..3] out of bounds.
    if (nalen < 4) continue;
    if (type == 7 && sps.empty()) sps.assign(nal, nal + nalen);
    if (type == 8 && pps.empty()) pps.assign(nal, nal + nalen);
  }
  if (sps.empty() || pps.empty()) return Status::kEncodeFailed;

  // avcC: SPS/PPS include their NAL header byte (ISO 14496-15).
  out->clear();
  out->push_back(1);  // configurationVersion
  out->push_back(sps[1]);
  out->push_back(sps[2]);
  out->push_back(sps[3]);
  out->push_back(0xFF);      // lengthSizeMinusOne = 3 (4-byte NAL lengths)
  out->push_back(0xE0 | 1);  // numOfSPS
  out->push_back(static_cast<uint8_t>(sps.size() >> 8));
  out->push_back(static_cast<uint8_t>(sps.size() & 0xFF));
  out->insert(out->end(), sps.begin(), sps.end());
  out->push_back(1);  // numOfPPS
  out->push_back(static_cast<uint8_t>(pps.size() >> 8));
  out->push_back(static_cast<uint8_t>(pps.size() & 0xFF));
  out->insert(out->end(), pps.begin(), pps.end());
  return Status::kOk;
}

Status FFmpegMuxer::AnnexBToAvcc(const uint8_t* data, size_t size,
                                 std::vector<uint8_t>* out) {
  out->clear();
  const uint8_t* p = data;
  const uint8_t* end = data + size;
  while (p < end) {
    int sc;
    if (!IsStartCode(p, end, &sc)) {
      ++p;
      continue;
    }
    p += sc;
    const uint8_t* nal = p;
    while (p < end && !IsStartCode(p, end, &sc)) ++p;
    const size_t nalen = static_cast<size_t>(p - nal);
    if (nalen == 0) continue;
    const int type = NalType(nal[0]);
    if (type == 7 || type == 8) continue;  // SPS/PPS live in extradata only
    AppendLenPrefixed(out, nal, nalen);
  }
  return out->empty() ? Status::kEncodeFailed : Status::kOk;
}

Status FFmpegMuxer::OpenMuxer(const VideoPacket& first_keyframe) {
  if (avformat_alloc_output_context2(&fmt_, nullptr, "mp4", nullptr) < 0 ||
      !fmt_) {
    fmt_ = nullptr;
    return Status::kEncodeFailed;
  }
  AVStream* st = avformat_new_stream(fmt_, nullptr);
  if (!st) {
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    return Status::kEncodeFailed;
  }
  st->id = 0;
  const int fps = config_.fps > 0 ? config_.fps : 30;
  st->time_base = AVRational{1, fps};
  AVCodecParameters* par = st->codecpar;
  par->codec_type = AVMEDIA_TYPE_VIDEO;
  par->codec_id = AV_CODEC_ID_H264;
  par->width = config_.width;
  par->height = config_.height;

  std::vector<uint8_t> extradata;
  if (BuildExtradata(first_keyframe.data.data(), first_keyframe.data.size(),
                     &extradata) == Status::kOk) {
    par->extradata = static_cast<uint8_t*>(
        av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!par->extradata) {
      avformat_free_context(fmt_);
      fmt_ = nullptr;
      return Status::kEncodeFailed;
    }
    std::memcpy(par->extradata, extradata.data(), extradata.size());
    par->extradata_size = static_cast<int>(extradata.size());
  }

  // Route all output through the attached ByteSink via a custom AVIOContext.
  uint8_t* iobuf = static_cast<uint8_t*>(av_malloc(kIoBufferSize));
  if (!iobuf) {
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    return Status::kEncodeFailed;
  }
  fmt_->pb = avio_alloc_context(iobuf, kIoBufferSize, 1, this, nullptr,
                                &FFmpegMuxer::SinkWrite, nullptr);
  if (!fmt_->pb) {
    av_free(iobuf);
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    return Status::kEncodeFailed;
  }
  // Fragmented mode writes the header (ftyp + moov) upfront and emits one
  // fragment per keyframe — sequential, no seeking needed.
  AVDictionary* opts = nullptr;
  if (config_.fragmented) {
    av_dict_set(&opts, "movflags", "frag_keyframe", 0);
  }
  const int hdr_ret = avformat_write_header(fmt_, &opts);
  av_dict_free(&opts);
  if (hdr_ret < 0) {
    avio_context_free(&fmt_->pb);
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    return Status::kEncodeFailed;
  }
  stream_index_ = st->index;
  opened_ = true;
  return Status::kOk;
}

Status FFmpegMuxer::Push(VideoPacket&& pkt) {
  if (pkt.data.empty()) return Status::kOk;
  if (!sink_) return Status::kInvalidArgument;

  if (!opened_) {
    // Wait for the first keyframe: the Annex-B bsf prepends SPS/PPS there.
    if (!pkt.keyframe) return Status::kOk;
    Status s = OpenMuxer(pkt);
    if (s != Status::kOk) return s;
  }

  std::vector<uint8_t> sample;
  if (AnnexBToAvcc(pkt.data.data(), pkt.data.size(), &sample) != Status::kOk) {
    return Status::kEncodeFailed;
  }

  // A refcounted packet is safe: the mov muxer buffers samples via
  // av_packet_ref until the trailer is written.
  AVPacket* avpkt = av_packet_alloc();
  if (!avpkt) return Status::kEncodeFailed;
  if (av_new_packet(avpkt, static_cast<int>(sample.size())) < 0) {
    av_packet_free(&avpkt);
    return Status::kEncodeFailed;
  }
  std::memcpy(avpkt->data, sample.data(), sample.size());
  avpkt->stream_index = stream_index_;
  const AVRational tb = fmt_->streams[stream_index_]->time_base;
  const int64_t pts = av_rescale_q(pkt.pts_us, AVRational{1, 1'000'000}, tb);
  avpkt->pts = pts;
  avpkt->dts = pts;  // encode order == display order (max_b_frames = 0)
  avpkt->flags = pkt.keyframe ? AV_PKT_FLAG_KEY : 0;

  // Fragmented mode: write each sample immediately (single stream, no
  // interleaving needed); on a keyframe boundary the mov muxer completes a
  // fragment, so flush the io buffer to the sink — the commit point for
  // unstable/streaming sinks. Non-fragmented keeps the interleaved path
  // (moov at end).
  int ret;
  if (config_.fragmented) {
    ret = av_write_frame(fmt_, avpkt);
    if (ret >= 0 && pkt.keyframe) {
      avio_flush(fmt_->pb);  // push the completed fragment bytes to the sink
      if (sink_ && !sink_->Flush()) ret = -1;  // commit point
    }
  } else {
    ret = av_interleaved_write_frame(fmt_, avpkt);
  }
  av_packet_free(&avpkt);
  return ret < 0 ? Status::kEncodeFailed : Status::kOk;
}

Status FFmpegMuxer::Flush() { return Status::kOk; }

Status FFmpegMuxer::Finish() {
  if (opened_) {
    av_write_trailer(fmt_);
    if (fmt_->pb) {
      avio_flush(fmt_->pb);
      avio_context_free(&fmt_->pb);
    }
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    opened_ = false;
    if (sink_ && !sink_->Flush()) return Status::kEncodeFailed;
  }
  return Status::kOk;
}

void FFmpegMuxer::Release() {
  if (opened_) {
    av_write_trailer(fmt_);
    if (fmt_->pb) {
      avio_flush(fmt_->pb);
      avio_context_free(&fmt_->pb);
    }
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    opened_ = false;
    if (sink_) sink_->Flush();
  }
  sink_ = nullptr;
}

}  // namespace codec
}  // namespace video
