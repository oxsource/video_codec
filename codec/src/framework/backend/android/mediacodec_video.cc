// mediacodec_video.cc
#include "src/framework/backend/android/mediacodec_video.h"

#include <chrono>
#include <cstring>
#include <thread>

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include "src/framework/api/codec_factory.h"
#include "src/framework/backend/android/mediacodec_utils.h"
#include "src/framework/core/packet_sink.h"

namespace video {
namespace codec {

namespace {

// AMediaCodec does not expose the key-frame flag as a named constant; the
// MediaCodec BUFFER_FLAG_KEY_FRAME value is 1.
constexpr uint32_t kKeyFrameFlag = 1;

  // Timeout for dequeue calls while polling for buffers (microseconds).
  constexpr int64_t kDequeueTimeoutUs = 10'000;

  // Drain deadline for the surface path (microseconds). EOS-after-
  // signalEndOfInputStream is not guaranteed across Android encoders, so the
  // surface Flush must not block forever (research R3).
  constexpr int64_t kEosDrainDeadlineUs = 5'000'000;

  // Wait for an input buffer slot, returns its index or -1.
  ssize_t DequeueInput(AMediaCodec* codec) {
    return AMediaCodec_dequeueInputBuffer(codec, kDequeueTimeoutUs);
  }

}  // namespace

MediaCodecVideoEncoder::MediaCodecVideoEncoder(const VideoConfig& config) : config_(config) {}

MediaCodecVideoEncoder::~MediaCodecVideoEncoder() { Release(); }

Status MediaCodecVideoEncoder::Init() {
  // Guard the entry state WITHOUT committing: the lifecycle flips only after
  // the real MediaCodec init succeeds, so a failed Init leaves the encoder
  // reusable in its previous state.
  if (!lifecycle_.CanInit()) return Status::kInvalidArgument;
  if (!config_.IsValid()) return Status::kInvalidArgument;

  const char* mime = android::MimeFor(config_.codec);
  if (!mime) return Status::kUnsupportedFormat;
  codec_.reset(AMediaCodec_createEncoderByType(mime));
  if (!codec_) return Status::kPlatformUnsupported;

  // Surface mode configures the encoder with COLOR_FormatSurface and feeds it
  // through the hardware input surface (zero-copy) instead of CPU frames.
  const int color_format =
      SurfaceMode() ? android::kColorFormatSurface : android::ColorFormatFor(config_.input_format);
  if (color_format == 0) {
    Release();
    return Status::kUnsupportedFormat;
  }

  format_.reset(AMediaFormat_new());
  AMediaFormat_setString(format_.get(), AMEDIAFORMAT_KEY_MIME, mime);
  AMediaFormat_setInt32(format_.get(), AMEDIAFORMAT_KEY_WIDTH, config_.width);
  AMediaFormat_setInt32(format_.get(), AMEDIAFORMAT_KEY_HEIGHT, config_.height);
  AMediaFormat_setInt32(format_.get(), AMEDIAFORMAT_KEY_COLOR_FORMAT, color_format);
  AMediaFormat_setInt32(format_.get(), AMEDIAFORMAT_KEY_BIT_RATE, config_.bitrate);
  const int fps = config_.fps > 0 ? config_.fps : 30;
  AMediaFormat_setInt32(format_.get(), AMEDIAFORMAT_KEY_FRAME_RATE, fps);
  AMediaFormat_setInt32(format_.get(), AMEDIAFORMAT_KEY_I_FRAME_INTERVAL,
                       config_.gop_size > 0 ? config_.gop_size : fps);

  const media_status_t cfg = AMediaCodec_configure(codec_.get(), format_.get(), nullptr, nullptr,
                                                   AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
  if (cfg != AMEDIA_OK) {
    Release();
    return Status::kEncodeFailed;
  }

  if (SurfaceMode()) {
    // Create the hardware input surface (zero-copy) BEFORE start() — MediaCodec
    // requires createInputSurface in the initialized state (INVALID_OPERATION
    // otherwise). The caller draws into it (ANativeWindow* via
    // CreateInputSurface) and the system delivers buffers to the encoder. The
    // window reference is owned by the codec; the caller must not release it
    // (contract C-004). On failure the encoder reports kPlatformUnsupported
    // and stays unusable for surface input (C-040).
    if (AMediaCodec_createInputSurface(codec_.get(), &surface_window_) != AMEDIA_OK ||
        !surface_window_) {
      Release();
      return Status::kPlatformUnsupported;
    }
  }

  if (AMediaCodec_start(codec_.get()) != AMEDIA_OK) {
    Release();
    return Status::kEncodeFailed;
  }

  // All real init succeeded — only now commit the lifecycle transition.
  if (lifecycle_.Init() != Status::kOk) {  // unreachable given the top guard
    Release();
    return Status::kInvalidArgument;
  }
  // Surface input is "encoding" from creation: mark it so Poll()/Flush() work
  // even if the caller never draws before flushing (idempotent).
  if (SurfaceMode()) lifecycle_.Encode();
  return Status::kOk;
}

Status MediaCodecVideoEncoder::QueueInput(const VideoFrame& frame) {
  if (frame.width != config_.width || frame.height != config_.height) {
    return Status::kInvalidArgument;
  }
  const ssize_t idx = DequeueInput(codec_.get());
  if (idx < 0) return Status::kEncodeFailed;

  size_t buf_size = 0;
  uint8_t* buf = AMediaCodec_getInputBuffer(codec_.get(), static_cast<size_t>(idx), &buf_size);
  const size_t need = android::BufferSizeFor(config_.width, config_.height, config_.input_format);
  if (!buf || buf_size < need) {
    // MediaCodec has no releaseInputBuffer: return the dequeued slot by
    // queueing an empty buffer so it is not leaked/exhausted on repeated
    // failures (and never misuse releaseOutputBuffer on an input index).
    AMediaCodec_queueInputBuffer(codec_.get(), static_cast<size_t>(idx), 0, 0, 0, 0);
    return Status::kEncodeFailed;
  }

  // Tight-packed copy of the framework planes into the MediaCodec input buffer
  // (planar I420: Y/U/V; semi-planar NV12: Y + interleaved UV).
  const int w = config_.width;
  const int h = config_.height;
  const int y_stride = frame.stride[0] > 0 ? frame.stride[0] : w;
  for (int y = 0; y < h; ++y) {
    std::memcpy(buf + static_cast<size_t>(y) * w, frame.planes[0].data() + y * y_stride,
                static_cast<size_t>(w));
  }
  if (config_.input_format == PixelFormat::kI420) {
    const int c_stride = frame.stride[1] > 0 ? frame.stride[1] : w / 2;
    const size_t u_off = static_cast<size_t>(w) * h;
    const size_t v_off = u_off + static_cast<size_t>(w / 2) * (h / 2);
    for (int y = 0; y < h / 2; ++y) {
      std::memcpy(buf + u_off + static_cast<size_t>(y) * (w / 2),
                  frame.planes[1].data() + y * c_stride, static_cast<size_t>(w / 2));
      std::memcpy(buf + v_off + static_cast<size_t>(y) * (w / 2),
                  frame.planes[2].data() + y * c_stride, static_cast<size_t>(w / 2));
    }
  } else {  // NV12: interleaved UV
    const int c_stride = frame.stride[1] > 0 ? frame.stride[1] : w;
    const size_t uv_off = static_cast<size_t>(w) * h;
    for (int y = 0; y < h / 2; ++y) {
      std::memcpy(buf + uv_off + static_cast<size_t>(y) * w,
                  frame.planes[1].data() + y * c_stride, static_cast<size_t>(w));
    }
  }

  // Presentation timestamp in microseconds, directly from the framework frame.
  const int64_t pts_us = frame.timestamp_us > 0 ? frame.timestamp_us : pts_;
  return AMediaCodec_queueInputBuffer(codec_.get(), static_cast<size_t>(idx), 0, need,
                                      static_cast<uint64_t>(pts_us), 0) == AMEDIA_OK
             ? Status::kOk
             : Status::kEncodeFailed;
}

Result<VideoPacket> MediaCodecVideoEncoder::Encode(const VideoFrame& frame) {
  // Input-mode mutual exclusion (contract C-012): surface mode feeds the codec
  // through the hardware input surface only; CPU frames are rejected.
  if (SurfaceMode()) return Err<VideoPacket>(Status::kUnsupportedOperation);
  if (lifecycle_.Encode() != Status::kOk) return Err<VideoPacket>(Status::kNotInitialized);
  if (QueueInput(frame) != Status::kOk) return Err<VideoPacket>(Status::kEncodeFailed);
  // Advance the fallback presentation clock by one frame's duration (µs) so
  // untimestamped input still yields real-time-spaced PTS (frame-count units
  // would be read as µs and play back near-instantaneously).
  const int fps = config_.fps > 0 ? config_.fps : 30;
  pts_ += 1'000'000 / fps;
  return Drain(/*drain_eof=*/false);
}

Result<VideoPacket> MediaCodecVideoEncoder::Encode(const NativeBuffer&) {
  // v1: CPU input path only; no hardware surface / native-buffer import.
  // Surface mode likewise rejects this (input-mode mutual exclusion, C-012).
  return Err<VideoPacket>(Status::kUnsupportedOperation);
}

void* MediaCodecVideoEncoder::CreateInputSurface() {
  // nullptr until Init() created the surface, and after Release() (which clears
  // surface_window_). Idempotent: repeated calls return the same handle
  // (contract C-004/C-005). Non-surface (CPU) mode never creates a window and
  // therefore always returns nullptr (C-002/C-003).
  return static_cast<void*>(surface_window_);
}

Status MediaCodecVideoEncoder::Poll() {
  // Surface-mode pump: drain ready output so the encoder keeps consuming input
  // surface frames (hardware encoders stall when their output queue fills,
  // which back-pressures the input surface — research R7). In push mode the
  // drained packets go to the sink; the pull-mode return is discarded here.
  if (!SurfaceMode()) return Status::kOk;
  if (lifecycle_.Encode() != Status::kOk) return Status::kNotInitialized;
  Result<VideoPacket> r = Drain(/*drain_eof=*/false);
  return r.ok() ? Status::kOk : r.status();
}

Result<VideoPacket> MediaCodecVideoEncoder::Flush() {
  if (lifecycle_.Flush() != Status::kOk) return Err<VideoPacket>(Status::kNotInitialized);

  if (SurfaceMode()) {
    // Surface input has no input buffer: signal end-of-input-stream, then drain.
    // EOS output after signalEndOfInputStream is NOT guaranteed across Android
    // encoders, so drain with a deadline instead of blocking forever (R3).
    AMediaCodec_signalEndOfInputStream(codec_.get());
    Result<VideoPacket> r = Drain(/*drain_eof=*/true, /*deadline_us=*/kEosDrainDeadlineUs);
    if (sink_) sink_->Flush();
    return r;
  }

  // Signal end-of-stream so every buffered frame is emitted, then drain. The
  // codec consumes input asynchronously, so retry briefly if no input slot is
  // immediately free — skipping EOS would silently drop buffered frames.
  ssize_t idx = -1;
  for (int attempt = 0; attempt < 4 && idx < 0; ++attempt) {
    idx = DequeueInput(codec_.get());
  }
  if (idx >= 0) {
    AMediaCodec_queueInputBuffer(codec_.get(), static_cast<size_t>(idx), 0, 0,
                                 static_cast<uint64_t>(pts_),
                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
  }
  Result<VideoPacket> r = Drain(/*drain_eof=*/true);
  // Reset for potential reuse (a MediaCodec in EOS state rejects new input).
  AMediaCodec_flush(codec_.get());
  if (sink_) sink_->Flush();
  return r;
}

Status MediaCodecVideoEncoder::SetOutputSink(PacketSink* sink) {
  sink_ = sink;
  return Status::kOk;
}

Result<VideoPacket> MediaCodecVideoEncoder::Drain(bool drain_eof, int64_t deadline_us) {
  VideoPacket out;  // pull-mode result (push mode returns an empty packet)
  const auto start = std::chrono::steady_clock::now();
  for (;;) {
    AMediaCodecBufferInfo info{};
    const ssize_t idx = AMediaCodec_dequeueOutputBuffer(codec_.get(), &info, kDequeueTimeoutUs);
    if (idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      // Poll-once for the normal path. With a deadline (surface EOS drain),
      // keep polling until the deadline expires — EOS-after-
      // signalEndOfInputStream is not guaranteed across Android encoders, so
      // this drain must terminate (research R3).
      if (drain_eof && deadline_us > 0) {
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() >=
            deadline_us) {
          break;  // deadline reached
        }
        continue;
      }
      break;  // nothing ready
    }
    if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED ||
        idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
      continue;
    }
    if (idx < 0) return Err<VideoPacket>(Status::kEncodeFailed);

    // Defensive: a malformed buffer with a negative offset/size would wrap to
    // a huge size_t below and read out of bounds. Release and skip it.
    if (info.offset < 0 || info.size < 0) {
      AMediaCodec_releaseOutputBuffer(codec_.get(), static_cast<size_t>(idx), false);
      continue;
    }

    size_t out_size = 0;
    uint8_t* raw = AMediaCodec_getOutputBuffer(codec_.get(), static_cast<size_t>(idx), &out_size);
    const uint8_t* payload = raw + info.offset;
    const size_t payload_size = static_cast<size_t>(info.size);

    if ((info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0 && payload_size > 0) {
      // Capture SPS/PPS from the codec-config buffer for keyframe assembly.
      std::vector<std::vector<uint8_t>> units;
      if (android::SplitAnnexB(payload, payload_size, &units)) {
        if (!units.empty()) sps_ = std::move(units[0]);
        if (units.size() > 1) pps_ = std::move(units[1]);
      }
      AMediaCodec_releaseOutputBuffer(codec_.get(), static_cast<size_t>(idx), false);
      continue;
    }

    if (payload_size > 0) {
      VideoPacket pkt;
      const bool keyframe = (info.flags & kKeyFrameFlag) != 0;
      // The codec keyframe payload already carries SPS/PPS on this platform
      // (Amlogic: [sc]SPS [sc]PPS [sc]IDR, confirmed on device). Prepend the
      // captured SPS/PPS only when the payload does NOT start with an SPS NAL
      // (other encoders keep SPS/PPS solely in the CODEC_CONFIG buffer).
      if (keyframe && !android::StartsWithNal(payload, payload_size, 7)) {
        // Strip a leading start code from the payload first: prepending SPS/PPS
        // around a payload that itself starts with a start code would produce a
        // double start code (an empty NAL for the muxer's writer).
        std::vector<uint8_t> unit(payload, payload + payload_size);
        if (unit.size() >= 4 && unit[0] == 0 && unit[1] == 0 && unit[2] == 0 && unit[3] == 1) {
          unit.erase(unit.begin(), unit.begin() + 4);
        } else if (unit.size() >= 3 && unit[0] == 0 && unit[1] == 0 && unit[2] == 1) {
          unit.erase(unit.begin(), unit.begin() + 3);
        }
        android::AppendAnnexB(keyframe, sps_, pps_, unit, &pkt.data);
      } else {
        pkt.data.assign(payload, payload + payload_size);
      }
      pkt.pts_us = info.presentationTimeUs;
      pkt.keyframe = keyframe;
      AMediaCodec_releaseOutputBuffer(codec_.get(), static_cast<size_t>(idx), false);
      if (sink_) {
        // Push mode: single destination is the sink.
        if (sink_->Push(std::move(pkt)) != Status::kOk) {
          return Err<VideoPacket>(Status::kEncodeFailed);
        }
      } else {
        out = std::move(pkt);
      }
    } else {
      AMediaCodec_releaseOutputBuffer(codec_.get(), static_cast<size_t>(idx), false);
    }

    if (drain_eof && (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
      break;  // EOS reached
    }
  }
  return Ok(std::move(out));
}

void MediaCodecVideoEncoder::Release() {
  lifecycle_.Release();
  sink_ = nullptr;  // non-owning; caller owns the sink lifetime
  surface_window_ = nullptr;  // input-surface handle invalidates after Release (C-004)
  if (codec_) AMediaCodec_stop(codec_.get());
  codec_.reset();  // AMediaCodec_delete via the RAII deleter
  format_.reset();
  sps_.clear();
  pps_.clear();
}

}  // namespace codec
}  // namespace video

// Self-registration (atlas-style); `mediacodec_video` carries alwayslink and
// is android-only (target_compatible_with), so this only compiles on Android.
namespace video {
namespace codec {
VIDEO_CODEC_REGISTER_VIDEO(Backend::kAndroid, MediaCodecVideoEncoder)
}  // namespace codec
}  // namespace video
