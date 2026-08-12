// mp4_mux_consumer.cc
#include "mux/mp4_mux_consumer.h"

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace video {
namespace codec {

namespace {

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

Mp4MuxConsumer::Mp4MuxConsumer(std::string path, int width, int height, int fps)
    : path_(std::move(path)),
      width_(width),
      height_(height),
      fps_(fps > 0 ? fps : 30) {}

Mp4MuxConsumer::~Mp4MuxConsumer() {
  if (opened_) {
    av_write_trailer(fmt_);
    if (fmt_ && fmt_->pb) avio_closep(&fmt_->pb);
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    opened_ = false;
  }
}

StatusCode Mp4MuxConsumer::BuildExtradata(const uint8_t* data, size_t size,
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
    if (type == 7 && sps.empty()) sps.assign(nal, nal + nalen);
    if (type == 8 && pps.empty()) pps.assign(nal, nal + nalen);
  }
  if (sps.empty() || pps.empty()) return StatusCode::kEncodeFailed;

  // avcC: SPS/PPS include their NAL header byte (ISO 14496-15).
  out->clear();
  out->push_back(1);  // configurationVersion
  out->push_back(sps[1]);
  out->push_back(sps[2]);
  out->push_back(sps[3]);
  out->push_back(0xFF);  // lengthSizeMinusOne = 3 (4-byte NAL lengths)
  out->push_back(0xE0 | 1);  // numOfSPS
  out->push_back(static_cast<uint8_t>(sps.size() >> 8));
  out->push_back(static_cast<uint8_t>(sps.size() & 0xFF));
  out->insert(out->end(), sps.begin(), sps.end());
  out->push_back(1);  // numOfPPS
  out->push_back(static_cast<uint8_t>(pps.size() >> 8));
  out->push_back(static_cast<uint8_t>(pps.size() & 0xFF));
  out->insert(out->end(), pps.begin(), pps.end());
  return StatusCode::kOk;
}

StatusCode Mp4MuxConsumer::AnnexBToAvcc(const uint8_t* data, size_t size,
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
  return out->empty() ? StatusCode::kEncodeFailed : StatusCode::kOk;
}

StatusCode Mp4MuxConsumer::OpenMuxer(const EncodedPacket& first_keyframe) {
  if (avformat_alloc_output_context2(&fmt_, nullptr, "mp4", path_.c_str()) < 0 ||
      !fmt_) {
    fmt_ = nullptr;
    return StatusCode::kEncodeFailed;
  }
  AVStream* st = avformat_new_stream(fmt_, nullptr);
  if (!st) {
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    return StatusCode::kEncodeFailed;
  }
  st->id = 0;
  st->time_base = AVRational{1, fps_};
  AVCodecParameters* par = st->codecpar;
  par->codec_type = AVMEDIA_TYPE_VIDEO;
  par->codec_id = AV_CODEC_ID_H264;
  par->width = width_;
  par->height = height_;

  std::vector<uint8_t> extradata;
  if (BuildExtradata(first_keyframe.data.data(), first_keyframe.data.size(),
                     &extradata) == StatusCode::kOk) {
    par->extradata =
        static_cast<uint8_t*>(av_mallocz(extradata.size() +
                                         AV_INPUT_BUFFER_PADDING_SIZE));
    if (!par->extradata) {
      avformat_free_context(fmt_);
      fmt_ = nullptr;
      return StatusCode::kEncodeFailed;
    }
    std::memcpy(par->extradata, extradata.data(), extradata.size());
    par->extradata_size = static_cast<int>(extradata.size());
  }

  if (!(fmt_->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&fmt_->pb, path_.c_str(), AVIO_FLAG_WRITE) < 0) {
      avformat_free_context(fmt_);
      fmt_ = nullptr;
      return StatusCode::kEncodeFailed;
    }
  }
  if (avformat_write_header(fmt_, nullptr) < 0) {
    avio_closep(&fmt_->pb);
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    return StatusCode::kEncodeFailed;
  }
  stream_index_ = st->index;
  opened_ = true;
  return StatusCode::kOk;
}

StatusCode Mp4MuxConsumer::Consume(EncodedPacket&& pkt) {
  if (pkt.data.empty()) return StatusCode::kOk;

  if (!opened_) {
    // Wait for the first keyframe: the Annex-B bsf prepends SPS/PPS there.
    if (!pkt.keyframe) return StatusCode::kOk;
    StatusCode s = OpenMuxer(pkt);
    if (s != StatusCode::kOk) return s;
  }

  std::vector<uint8_t> sample;
  if (AnnexBToAvcc(pkt.data.data(), pkt.data.size(), &sample) !=
      StatusCode::kOk) {
    return StatusCode::kEncodeFailed;
  }

  // A refcounted packet is safe: the mov muxer buffers samples via
  // av_packet_ref until the trailer is written.
  AVPacket* avpkt = av_packet_alloc();
  av_new_packet(avpkt, static_cast<int>(sample.size()));
  std::memcpy(avpkt->data, sample.data(), sample.size());
  avpkt->stream_index = stream_index_;
  const AVRational tb = fmt_->streams[stream_index_]->time_base;
  const int64_t pts = av_rescale_q(pkt.pts_us, AVRational{1, 1'000'000}, tb);
  avpkt->pts = pts;
  avpkt->dts = pts;  // encode order == display order (max_b_frames = 0)
  avpkt->flags = pkt.keyframe ? AV_PKT_FLAG_KEY : 0;

  // Use av_interleaved_write_frame (not av_write_frame): the mov/mp4 muxer
  // buffers/interleaves samples, and the interleaved API keeps the sample
  // tables (stsz/stco) consistent with the data written to mdat.
  const int ret = av_interleaved_write_frame(fmt_, avpkt);
  av_packet_free(&avpkt);
  return ret < 0 ? StatusCode::kEncodeFailed : StatusCode::kOk;
}

StatusCode Mp4MuxConsumer::Finish() {
  if (opened_) {
    av_write_trailer(fmt_);
    if (fmt_->pb) avio_closep(&fmt_->pb);
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    opened_ = false;
  }
  return StatusCode::kOk;
}

}  // namespace codec
}  // namespace video
