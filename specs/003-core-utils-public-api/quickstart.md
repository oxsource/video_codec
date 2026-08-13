# Quickstart: Core Utilities & Public API Surface (spec 003)

**Branch**: `003-core-utils-public-api` | **Date**: 2026-08-12
**Feature**: [spec.md](spec.md)

Short guide for developers consuming or extending the two pieces added by this feature:
the `utils` media helpers and the single public umbrella header.

## 1. Consuming the library (public surface)

Include exactly one header:

```cpp
#include <video_codec/video_codec.h>

int main() {
    // Build a config, create an encoder through the factory.
    video_codec::VideoEncoderConfig cfg;
    cfg.codec = video_codec::VideoCodecType::kH264;
    cfg.width = 1280;
    cfg.height = 720;
    // Returns nullptr if no backend is linked/registered for this config.
    std::unique_ptr<video_codec::VideoEncoder> encoder =
        video_codec::CreateVideoEncoder(cfg);
    if (!encoder) return 1;
    if (encoder->Init() != video_codec::Status::kOk) return 1;
    // ... feed frames via encoder->Encode(frame) ...
}
```

- No other header is needed or should be included by consumer code.
- To force a backend (debug/tests), set `cfg.force_backend` or call
  `video_codec::ResolveBackend(...)` directly.
- Public symbols are exported automatically for shared-library builds; static builds need
  no extra setup.

## 2. Using the utilities (within the framework)

`utils` depends only on `core`. Example: prepare a frame for the FFmpeg/AAC backend.

```cpp
#include "utils/stride.h"

// stride for a given width/format
size_t stride = video_codec::utils::Stride::Row(width, video_codec::PixelFormat::kNV12);
```

Pixel-format conversion is NOT in `utils` — it lives in the libyuv-backed
`convert/libyuv` module (cross-platform, available everywhere libyuv links):

```cpp
#include "convert/libyuv/pixel_convert.h"

// YUV420P (kI420) -> NV12
video_codec::VideoFrame nv12;
if (auto st = video_codec::PixelConverter::Convert(
        i420_frame, video_codec::PixelFormat::kNV12, nv12); !st.ok()) {
    // handle error status
}
```

PCM sample-format conversion is NOT in `utils` — it lives in the FFmpeg-backed
`backend/ffmpeg/swr` module (libswresample), available on non-Android builds:

```cpp
#include "backend/ffmpeg/swr/swr_audio_convert.h"

// PCM: interleaved s16 -> planar f32 (what FFmpeg AAC expects)
if (auto st = video_codec::SwrAudioConverter::Convert(
        s16_buf, video_codec::SampleFormat::kF32Planar, out); !st.ok()) {
    // handle error status
}
```

## 3. Building & testing

From the `codec/` workspace root (Bazel 6.5.0):

```bash
bazel build //src/framework/...        # builds utils + public + dependents
bazel test  //tests/utils/...          # conversion round-trip / stride / pcm tests
bazel test  //src/framework/public/... # header-only compile test
```

## 4. What is NOT in this feature

- The core types, error model, `LogSlot`, and abstract encoder interfaces (already shipped
  in an earlier commit).
- Backend implementations, output transport (queue/consumer), and the encode-to-file
  example (tracked separately).
- RGB↔YUV conversion and audio resampling (future extensions; see `research.md` R1/R3).
