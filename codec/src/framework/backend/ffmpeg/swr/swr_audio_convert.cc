// swr_audio_convert.cc
#include "swr_audio_convert.h"

// FFmpeg 6.1 public headers carry no C++ linkage guards; wrap them in
// extern "C" so the calls bind to the archive's C symbols (mirrors
// backend/ffmpeg/video_encoder.cc).
extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <cstdint>
#include <cstring>
#include <vector>

namespace video {
namespace codec {

namespace {

AVSampleFormat ToAvFormat(SampleFormat fmt) {
  switch (fmt) {
    case SampleFormat::kS16:
      return AV_SAMPLE_FMT_S16;
    case SampleFormat::kF32:
      return AV_SAMPLE_FMT_FLT;
    case SampleFormat::kS16Planar:
      return AV_SAMPLE_FMT_S16P;
    case SampleFormat::kF32Planar:
      return AV_SAMPLE_FMT_FLTP;
  }
  return AV_SAMPLE_FMT_NONE;
}

}  // namespace

int SwrAudioConverter::BytesPerSample(SampleFormat fmt) {
  switch (fmt) {
    case SampleFormat::kS16:
    case SampleFormat::kS16Planar:
      return 2;
    case SampleFormat::kF32:
    case SampleFormat::kF32Planar:
      return 4;
  }
  return 0;
}

bool SwrAudioConverter::IsPlanar(SampleFormat fmt) {
  return fmt == SampleFormat::kS16Planar || fmt == SampleFormat::kF32Planar;
}

Status SwrAudioConverter::Convert(const AudioFrame& src, SampleFormat dst_format, AudioFrame& dst) {
  if (src.channels <= 0 || src.sample_rate <= 0) {
    return Status::kInvalidArgument;
  }
  const AVSampleFormat in_fmt = ToAvFormat(src.format);
  const AVSampleFormat out_fmt = ToAvFormat(dst_format);
  if (in_fmt == AV_SAMPLE_FMT_NONE || out_fmt == AV_SAMPLE_FMT_NONE) {
    return Status::kUnsupportedFormat;
  }

  const int in_bps = av_get_bytes_per_sample(in_fmt);
  const int out_bps = av_get_bytes_per_sample(out_fmt);
  const size_t frame_bytes = static_cast<size_t>(in_bps) * src.channels;
  const int n = frame_bytes == 0 ? 0 : static_cast<int>(src.data.size() / frame_bytes);

  // Frame metadata is preserved; only the sample format changes.
  dst.format = dst_format;
  dst.sample_rate = src.sample_rate;
  dst.channels = src.channels;
  dst.timestamp_us = src.timestamp_us;
  dst.data.clear();
  if (n == 0) return Status::kOk;

  // Same sample rate + channel layout on both sides: swresample performs a
  // pure format conversion, including interleaved <-> planar repacking.
  AVChannelLayout in_layout, out_layout;
  av_channel_layout_default(&in_layout, src.channels);
  av_channel_layout_default(&out_layout, src.channels);

  SwrContext* swr = nullptr;
  // swr_alloc_set_opts2 frees the context and sets `swr` to null on failure;
  // never call swr_init on a null context.
  if (swr_alloc_set_opts2(&swr, &out_layout, out_fmt, src.sample_rate, &in_layout, in_fmt,
                          src.sample_rate, 0, nullptr) < 0 ||
      swr == nullptr || swr_init(swr) < 0) {
    swr_free(&swr);
    av_channel_layout_uninit(&in_layout);
    av_channel_layout_uninit(&out_layout);
    return Status::kEncodeFailed;
  }
  av_channel_layout_uninit(&in_layout);
  av_channel_layout_uninit(&out_layout);

  // Input planes: interleaved uses a single plane, planar one per channel.
  const bool in_planar = av_sample_fmt_is_planar(in_fmt);
  std::vector<const uint8_t*> in_planes(in_planar ? src.channels : 1);
  if (in_planar) {
    for (int c = 0; c < src.channels; ++c) {
      in_planes[c] = src.data.data() + static_cast<size_t>(c) * n * in_bps;
    }
  } else {
    in_planes[0] = src.data.data();
  }

  // Output buffers hold the same sample count (no resampling requested).
  const bool out_planar = av_sample_fmt_is_planar(out_fmt);
  const size_t planes = out_planar ? src.channels : 1;
  const size_t plane_bytes = static_cast<size_t>(n) * out_bps * (out_planar ? 1 : src.channels);
  std::vector<std::vector<uint8_t>> out_bufs(planes);
  std::vector<uint8_t*> out_planes(planes);
  for (size_t p = 0; p < planes; ++p) {
    out_bufs[p].resize(plane_bytes);
    out_planes[p] = out_bufs[p].data();
  }

  const int converted = swr_convert(swr, out_planes.data(), n, in_planes.data(), n);
  swr_free(&swr);
  if (converted < 0) return Status::kEncodeFailed;

  // Repack into the single-buffer AudioFrame convention.
  dst.data.resize(static_cast<size_t>(converted) * out_bps * src.channels);
  if (out_planar) {
    for (int c = 0; c < src.channels; ++c) {
      std::memcpy(dst.data.data() + static_cast<size_t>(c) * converted * out_bps, out_planes[c],
                  static_cast<size_t>(converted) * out_bps);
    }
  } else {
    std::memcpy(dst.data.data(), out_planes[0], dst.data.size());
  }
  return Status::kOk;
}

}  // namespace codec
}  // namespace video
