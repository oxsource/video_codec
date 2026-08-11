// mediacodec_spike.cc
//
// Minimal spike validating the Android NDK MediaCodec encode wiring compiles
// and drives the encoder end-to-end: create an AVC encoder, configure a 320x240
// NV12 format, queue one synthetic input buffer, and drain one output buffer.
//
// This is the Android counterpart of ffmpeg_spike. It is Android-only: the
// target is `target_compatible_with = ["@platforms//os:android"]`, so the host
// (Linux/macOS) build never compiles or links it and stays NDK-free. The real
// NDK headers come from `android_ndk_repository(name = "androidndk")`; the
// //third_party/android_ndk:android_media_codec wrapper forwards to them once
// that repository is registered (see third_party/android_ndk/BUILD.bazel).

#include <cstdio>

#if defined(__ANDROID__)
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#endif

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
// COLOR_FormatYUV420SemiPlanar (NV12) — standard input format for the AVC
// encoder on most devices.
constexpr int kColorFormatNV12 = 21;
constexpr int kBitrate = 400000;
constexpr int kFrameRate = 30;
constexpr int kIFrameInterval = 1;

}  // namespace

int main() {
#if defined(__ANDROID__)
  AMediaCodec* codec = AMediaCodec_createEncoderByType("video/avc");
  if (!codec) {
    std::fprintf(stderr, "spike: failed to create AVC encoder\n");
    return 1;
  }

  AMediaFormat* format = AMediaFormat_new();
  AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, kWidth);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, kHeight);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorFormatNV12);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, kBitrate);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, kFrameRate);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, kIFrameInterval);

  // AMEDIACODEC_CONFIGURE_FLAG_ENCODE == 1
  media_status_t status =
      AMediaCodec_configure(codec, format, nullptr, nullptr, 1);
  if (status != AMEDIA_OK) {
    std::fprintf(stderr, "spike: configure failed (status=%d)\n",
                 static_cast<int>(status));
    AMediaCodec_delete(codec);
    AMediaFormat_delete(format);
    return 1;
  }

  status = AMediaCodec_start(codec);
  if (status != AMEDIA_OK) {
    std::fprintf(stderr, "spike: start failed (status=%d)\n",
                 static_cast<int>(status));
    AMediaCodec_delete(codec);
    AMediaFormat_delete(format);
    return 1;
  }

  // Queue one synthetic NV12 input buffer.
  const long input_size = static_cast<long>(kWidth) * kHeight * 3 / 2;
  ssize_t in_idx = AMediaCodec_dequeueInputBuffer(codec, 1000000 /*us*/);
  bool queued = false;
  if (in_idx >= 0) {
    size_t buf_size = 0;
    uint8_t* buf = AMediaCodec_getInputBuffer(codec, in_idx, &buf_size);
    if (buf && buf_size >= static_cast<size_t>(input_size)) {
      for (long i = 0; i < input_size; ++i) {
        buf[i] = static_cast<uint8_t>(i & 0xFF);
      }
      status = AMediaCodec_queueInputBuffer(codec, in_idx, 0,
                                            static_cast<size_t>(input_size),
                                            0 /*us*/, 0 /*flags*/);
      queued = (status == AMEDIA_OK);
    }
  }

  // Drain one output buffer.
  bool drained = false;
  AMediaCodecBufferInfo info{};
  ssize_t out_idx = AMediaCodec_dequeueOutputBuffer(codec, &info, 1000000 /*us*/);
  if (out_idx >= 0) {
    size_t out_size = 0;
    uint8_t* out = AMediaCodec_getOutputBuffer(codec, out_idx, &out_size);
    std::fprintf(stderr,
                 "spike: got output buffer idx=%zd size=%d flags=%u\n",
                 out_idx, info.size, info.flags);
    drained = (out != nullptr);
    AMediaCodec_releaseOutputBuffer(codec, out_idx, false /*render*/);
  }

  AMediaCodec_stop(codec);
  AMediaCodec_delete(codec);
  AMediaFormat_delete(format);

  if (!queued || !drained) {
    std::fprintf(stderr, "spike: FAILED (queued=%d drained=%d)\n",
                 static_cast<int>(queued), static_cast<int>(drained));
    return 1;
  }
  std::fprintf(stderr, "spike: MediaCodec AVC encode path OK\n");
  return 0;
#else
  std::fprintf(stderr, "mediacodec_spike: compiled for Android only; "
                       "nothing to run on this host.\n");
  return 0;
#endif
}
