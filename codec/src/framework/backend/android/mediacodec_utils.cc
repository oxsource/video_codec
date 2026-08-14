// mediacodec_utils.cc
#include "mediacodec_utils.h"

namespace video {
namespace codec {
namespace android {
namespace {

constexpr uint8_t kStartCode[4] = {0, 0, 0, 1};

// Returns the Annex-B start-code length at `p` (3 or 4), or 0 if none.
int StartCodeLength(const uint8_t* p, const uint8_t* end) {
  if (end - p >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) return 4;
  if (end - p >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1) return 3;
  return 0;
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

int ColorFormatFor(PixelFormat format) {
  switch (format) {
    case PixelFormat::kI420:
      return 19;  // COLOR_FormatYUV420Planar
    case PixelFormat::kNV12:
      return 21;  // COLOR_FormatYUV420SemiPlanar
    default:
      return 0;
  }
}

const char* MimeFor(VideoCodecType codec) {
  switch (codec) {
    case VideoCodecType::kH264:
      return "video/avc";
    case VideoCodecType::kHEVC:
      return "video/hevc";
  }
  return nullptr;
}

const char* MimeFor(AudioCodecType codec) {
  switch (codec) {
    case AudioCodecType::kAAC:
      return "audio/mp4a-latm";
    default:
      return nullptr;  // kNone / kOpus (v1 unsupported)
  }
}

size_t BufferSizeFor(int width, int height, PixelFormat format) {
  if (width <= 0 || height <= 0) return 0;
  switch (format) {
    case PixelFormat::kI420:
      // 4:2:0 planar needs even dimensions for the half-size chroma planes.
      if (width % 2 != 0 || height % 2 != 0) return 0;
      return static_cast<size_t>(width) * height * 3 / 2;
    case PixelFormat::kNV12:
      // Semi-planar UV interleaved also assumes even dimensions (w x h/2).
      if (width % 2 != 0 || height % 2 != 0) return 0;
      return static_cast<size_t>(width) * height * 3 / 2;
    case PixelFormat::kRGBA:
      return static_cast<size_t>(width) * height * 4;
  }
  return 0;
}

void AppendAnnexB(bool keyframe, const std::vector<uint8_t>& sps,
                  const std::vector<uint8_t>& pps, const std::vector<uint8_t>& payload,
                  std::vector<uint8_t>* out) {
  if (keyframe) {
    if (!sps.empty()) {
      out->insert(out->end(), kStartCode, kStartCode + sizeof(kStartCode));
      out->insert(out->end(), sps.begin(), sps.end());
    }
    if (!pps.empty()) {
      out->insert(out->end(), kStartCode, kStartCode + sizeof(kStartCode));
      out->insert(out->end(), pps.begin(), pps.end());
    }
  }
  out->insert(out->end(), kStartCode, kStartCode + sizeof(kStartCode));
  out->insert(out->end(), payload.begin(), payload.end());
}

bool SplitAnnexB(const uint8_t* data, size_t size, std::vector<std::vector<uint8_t>>* units) {
  units->clear();
  if (!data || size == 0) return false;
  const uint8_t* p = data;
  const uint8_t* end = data + size;
  while (p < end) {
    const int sc = StartCodeLength(p, end);
    if (sc == 0) {
      ++p;
      continue;
    }
    p += sc;
    const uint8_t* nal = p;
    while (p < end && StartCodeLength(p, end) == 0) ++p;
    const size_t nalen = static_cast<size_t>(p - nal);
    if (nalen > 0) units->emplace_back(nal, nal + nalen);
  }
  return !units->empty();
}

bool BuildAudioSpecificConfig(int sample_rate, int channels, std::vector<uint8_t>* out) {
  const int freq_index = SamplingFrequencyIndex(sample_rate);
  if (freq_index < 0 || channels < 1 || channels > 7) return false;
  constexpr int kAacLc = 2;  // audioObjectType = AAC-LC
  out->clear();
  // 5 bits AOT, 4 bits samplingFrequencyIndex, 4 bits channelConfiguration.
  out->push_back(static_cast<uint8_t>(((kAacLc << 3) & 0xF8) | ((freq_index >> 1) & 0x07)));
  out->push_back(static_cast<uint8_t>(((freq_index & 1) << 7) | ((channels & 0x0F) << 3)));
  return true;
}

}  // namespace android
}  // namespace codec
}  // namespace video
