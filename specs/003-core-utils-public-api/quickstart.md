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
    if (encoder->Init() != video_codec::StatusCode::kOk) return 1;
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
#include "utils/yuv_convert.h"
#include "utils/stride.h"
#include "utils/pcm_convert.h"

// YUV420P (kI420) -> NV12
video_codec::VideoFrame nv12;
if (auto st = video_codec::utils::ConvertPixelFormat(i420_frame,
                                                     video_codec::PixelFormat::kNV12,
                                                     nv12); !st.ok()) {
    // handle error status
}

// stride for a given width/format
size_t stride = video_codec::utils::RowStride(width, video_codec::PixelFormat::kNV12);

// PCM: interleaved s16 -> planar f32 (what FFmpeg AAC expects)
if (auto st = video_codec::utils::ConvertSampleFormat(s16_buf,
                       video_codec::SampleFormat::kF32Planar, out); !st.ok()) {
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
