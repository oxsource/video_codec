// ffmpeg_muxer.cc
#include "codec/src/framework/backend/ffmpeg/ffmpeg_muxer.h"

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/mem.h>
}

#include "codec/src/framework/io/byte_sink.h"
#include "codec/src/framework/api/codec_factory.h"

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

void AppendLenPrefixed(std::vector<uint8_t>* out, const uint8_t* nal, size_t nalen) {
  out->push_back(static_cast<uint8_t>(nalen >> 24));
  out->push_back(static_cast<uint8_t>(nalen >> 16));
  out->push_back(static_cast<uint8_t>(nalen >> 8));
  out->push_back(static_cast<uint8_t>(nalen));
  out->insert(out->end(), nal, nal + nalen);
}

// libavformat container name for the requested format. Extend when new
// MuxFormat values are added (kMkv, kTs, kWebm).
const char* MuxFormatName(MuxFormat format) {
  switch (format) {
    case MuxFormat::kMp4:
      return "mp4";
  }
  return "mp4";  // unreachable for the current single-format enum
}

// Sampling-frequency index for the AAC AudioSpecificConfig (ISO 14496-3
// Table 1.15); -1 for rates the muxer cannot describe.
int SamplingFrequencyIndex(int sample_rate) {
  switch (sample_rate) {
    case 96000: return 0;
    case 88200: return 1;
    case 64000: return 2;
    case 48000: return 3;
    case 44100: return 4;
    case 32000: return 5;
    case 24000: return 6;
    case 22050: return 7;
    case 16000: return 8;
    case 12000: return 9;
    case 11025: return 10;
    case 8000: return 11;
    case 7350: return 12;
  }
  return -1;
}

}  // namespace

int FFmpegMuxer::SinkWrite(void* opaque, uint8_t* buf, int size) {
  auto* muxer = static_cast<FFmpegMuxer*>(opaque);
  if (!muxer->sink_ || !muxer->sink_->Write(buf, static_cast<size_t>(size))) {
    return AVERROR(EIO);
  }
  muxer->written_ += size;
  return size;
}

int64_t FFmpegMuxer::SinkSeek(void* opaque, int64_t offset, int whence) {
  auto* muxer = static_cast<FFmpegMuxer*>(opaque);
  if (!muxer->sink_) return -1;
  int64_t target = -1;
  switch (whence) {
    case SEEK_SET:
      target = offset;
      break;
    case SEEK_CUR:
      target = muxer->sink_->Tell() + offset;
      break;
    case SEEK_END:
      target = muxer->written_ + offset;  // file size == bytes written so far
      break;
    default:
      return -1;
  }
  if (target < 0 || !muxer->sink_->Seek(target)) return -1;
  return muxer->sink_->Tell();
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

Status FFmpegMuxer::BuildExtradata(const uint8_t* data, size_t size, std::vector<uint8_t>* out) {
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

Status FFmpegMuxer::AnnexBToAvcc(const uint8_t* data, size_t size, std::vector<uint8_t>* out) {
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
  if (avformat_alloc_output_context2(&fmt_, nullptr, MuxFormatName(config_.format), nullptr) < 0 ||
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
  if (BuildExtradata(first_keyframe.data.data(), first_keyframe.data.size(), &extradata) ==
      Status::kOk) {
    par->extradata =
        static_cast<uint8_t*>(av_mallocz(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!par->extradata) {
      avformat_free_context(fmt_);
      fmt_ = nullptr;
      return Status::kEncodeFailed;
    }
    std::memcpy(par->extradata, extradata.data(), extradata.size());
    par->extradata_size = static_cast<int>(extradata.size());
  }

  // Optional AAC-LC audio track: the mov muxer writes the esds descriptor
  // from this stream's codecpar + our 2-byte AudioSpecificConfig extradata
  // (raw AAC access units, no ADTS headers — exactly what MP4 expects).
  if (config_.audio_codec == AudioCodecType::kAAC) {
    AVStream* ast = avformat_new_stream(fmt_, nullptr);
    if (!ast) {
      avformat_free_context(fmt_);
      fmt_ = nullptr;
      return Status::kEncodeFailed;
    }
    ast->id = 1;
    ast->time_base = AVRational{1, config_.sample_rate};
    AVCodecParameters* apar = ast->codecpar;
    apar->codec_type = AVMEDIA_TYPE_AUDIO;
    apar->codec_id = AV_CODEC_ID_AAC;
    apar->sample_rate = config_.sample_rate;
    av_channel_layout_default(&apar->ch_layout, config_.channels);

    std::vector<uint8_t> asc;
    if (BuildAudioSpecificConfig(config_.sample_rate, config_.channels, &asc) != Status::kOk) {
      avformat_free_context(fmt_);
      fmt_ = nullptr;
      return Status::kEncodeFailed;
    }
    apar->extradata =
        static_cast<uint8_t*>(av_mallocz(asc.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!apar->extradata) {
      avformat_free_context(fmt_);
      fmt_ = nullptr;
      return Status::kEncodeFailed;
    }
    std::memcpy(apar->extradata, asc.data(), asc.size());
    apar->extradata_size = static_cast<int>(asc.size());
    audio_stream_index_ = ast->index;
  }

  // Route all output through the attached ByteSink via a custom AVIOContext.
  uint8_t* iobuf = static_cast<uint8_t*>(av_malloc(kIoBufferSize));
  if (!iobuf) {
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    return Status::kEncodeFailed;
  }
  fmt_->pb =
      avio_alloc_context(iobuf, kIoBufferSize, 1, this, nullptr, &FFmpegMuxer::SinkWrite,
                         &FFmpegMuxer::SinkSeek);
  if (!fmt_->pb) {
    av_free(iobuf);
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    return Status::kEncodeFailed;
  }
  // Fragmented mode writes the header (ftyp + moov) upfront and emits one
  // fragment per keyframe — sequential, no seeking needed. Non-fragmented mode
  // (moov at end) requires the seekable avio wired above.
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
    // Container is open now: replay any audio buffered before the first
    // keyframe (Await drains video first, so this is only a fallback).
    for (const AudioPacket& ap : pending_audio_) {
      if (WriteAudioPacket(ap) != Status::kOk) return Status::kEncodeFailed;
    }
    pending_audio_.clear();
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
  avpkt->duration = 1;  // one time_base tick = 1/fps (mov muxer needs it)
  avpkt->flags = pkt.keyframe ? AV_PKT_FLAG_KEY : 0;

  // Fragmented mode: write each sample immediately (single stream, no
  // interleaving needed); on a keyframe boundary the mov muxer completes a
  // fragment, so flush the io buffer to the sink — the commit point for
  // unstable/streaming sinks. Non-fragmented keeps the interleaved path
  // (moov at end).
  int ret;
  // av_interleaved_write_frame handles both modes: interleaved writes for the
  // non-fragmented path (moov at end) and fragment management for frag_keyframe
  // (single stream; raw av_write_frame leaves a trailing fragment without moof).
  ret = av_interleaved_write_frame(fmt_, avpkt);
  if (ret >= 0 && config_.fragmented && pkt.keyframe) {
    avio_flush(fmt_->pb);                    // push the completed fragment bytes to the sink
    if (sink_ && !sink_->Flush()) ret = -1;  // commit point
  }
  av_packet_free(&avpkt);
  return ret < 0 ? Status::kEncodeFailed : Status::kOk;
}

Status FFmpegMuxer::BuildAudioSpecificConfig(int sample_rate, int channels,
                                             std::vector<uint8_t>* out) {
  const int freq_index = SamplingFrequencyIndex(sample_rate);
  if (freq_index < 0 || channels < 1 || channels > 7) {
    return Status::kEncodeFailed;
  }
  // AAC-LC (audioObjectType 2) AudioSpecificConfig, ISO 14496-3: 5 bits AOT,
  // 4 bits samplingFrequencyIndex, 4 bits channelConfiguration.
  constexpr int kAacLc = 2;
  out->clear();
  out->push_back(static_cast<uint8_t>(((kAacLc << 3) & 0xF8) | ((freq_index >> 1) & 0x07)));
  out->push_back(static_cast<uint8_t>(((freq_index & 1) << 7) | ((channels & 0x0F) << 3)));
  return Status::kOk;
}

Status FFmpegMuxer::Push(AudioPacket&& pkt) {
  if (pkt.data.empty()) return Status::kOk;
  if (!sink_) return Status::kInvalidArgument;
  // v1: only AAC-LC is carried by the mov muxer (raw access units + ASC).
  if (config_.audio_codec != AudioCodecType::kAAC) {
    return Status::kUnsupportedOperation;
  }
  if (!opened_) {
    // Buffer until the container opens on the first video keyframe.
    pending_audio_.push_back(std::move(pkt));
    return Status::kOk;
  }
  return WriteAudioPacket(pkt);
}

Status FFmpegMuxer::WriteAudioPacket(const AudioPacket& pkt) {
  if (audio_stream_index_ < 0) return Status::kEncodeFailed;
  AVPacket* avpkt = av_packet_alloc();
  if (!avpkt) return Status::kEncodeFailed;
  if (av_new_packet(avpkt, static_cast<int>(pkt.data.size())) < 0) {
    av_packet_free(&avpkt);
    return Status::kEncodeFailed;
  }
  std::memcpy(avpkt->data, pkt.data.data(), pkt.data.size());
  avpkt->stream_index = audio_stream_index_;
  const AVRational tb = fmt_->streams[audio_stream_index_]->time_base;
  const int64_t pts = av_rescale_q(pkt.pts_us, AVRational{1, 1'000'000}, tb);
  avpkt->pts = pts;
  avpkt->dts = pts;  // encode order == display order (no codec delay in PTS)
  avpkt->flags = 0;  // audio is never a keyframe

  // The mov muxer buffers audio samples into the current fragment and writes
  // them when the next keyframe closes it; no per-packet io flush here.
  const int ret = config_.fragmented ? av_write_frame(fmt_, avpkt)
                                     : av_interleaved_write_frame(fmt_, avpkt);
  av_packet_free(&avpkt);
  return ret < 0 ? Status::kEncodeFailed : Status::kOk;
}

Status FFmpegMuxer::Flush() {
  // Packets still buffered pre-open (no video keyframe yet) are dropped:
  // without the avcC extradata there is no container to put them in.
  pending_audio_.clear();
  return Status::kOk;
}

Status FFmpegMuxer::Finish() {
  pending_audio_.clear();  // unreachable audio buffered without an open
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
  pending_audio_.clear();
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

// Self-registration (atlas-style); `ffmpeg_muxer` carries alwayslink.
namespace video {
namespace codec {
VIDEO_CODEC_REGISTER_MUXER(Backend::kFFmpeg, FFmpegMuxer)
}  // namespace codec
}  // namespace video
