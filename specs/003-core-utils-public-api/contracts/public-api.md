# Public API Contract: Umbrella Header & Export Surface (spec 003)

**Branch**: `003-core-utils-public-api` | **Date**: 2026-08-12
**Feature**: [spec.md](../spec.md) | **Data model**: [data-model.md](../data-model.md)

This contract defines the **single public include surface** a consumer uses to link against
the `video_codec` library. It complements (and does not duplicate) the encoder contract in
spec `002-architecture-engineering-design/contracts/public-api.md`; that contract defines the
*shape* of the encoder API, this contract defines the *packaging* (one header, one export
macro, one visible module).

## 1. Include contract

A consumer program MUST be able to do:

```cpp
#include <video_codec/video_codec.h>   // the ONLY header a consumer includes
```

and obtain, with no further includes, the full public surface:
`VideoEncoder`, `AudioEncoder`, the factory entry points (`CreateVideo`,
`CreateAudio`, `ResolveBackend`, `RegisterVideo`, `RegisterAudio`),
`VideoFrame`, `AudioFrame`,
`Packet`, `NativeBuffer`, `PixelFormat`, `SampleFormat`,
`VideoCodecType`, `AudioCodecType`, `Backend`, `Status`, `Result<T>`, `LogSlot`,
`InputSurface`, and the `*EncoderConfig` structs — the public API intended to be decorated
with `VIDEO_CODEC_API` (see §3).

## 2. Visibility contract

- `public` is the **only** `//visibility:public` module. Internal modules
  (`core`, `api`, `utils`, `backend/*`, `queue`, `consumer`) MUST NOT be reachable from
  consumer code (enforced by Bazel visibility; verified by the header-only compile test).
- The umbrella re-exports exactly the contracts listed in §1 and nothing internal.

## 3. Export macro contract

`VIDEO_CODEC_API` (from `video_codec_export.h`) governs symbol visibility:

| Build mode | `VIDEO_CODEC_API` expands to |
|-----------|------------------------------|
| Static library (default) | empty (no decoration) |
| Shared library (`-DVIDEO_CODEC_SHARED_LIBRARY`) | `__attribute__((visibility("default")))` (or `__declspec` on Windows) |

Public symbols MUST be tagged with `VIDEO_CODEC_API`; internal symbols MUST NOT be, and the
library is compiled with `-fvisibility=hidden` so untagged symbols stay internal.

**Current status**: static builds are the default distribution form and are unaffected
(the macro expands to nothing). Decorating the individual public API types with
`VIDEO_CODEC_API` for shared-library symbol export is a tracked follow-up; the umbrella
header and the header-only `public:video_codec_hdrs` target already carry the correct
include/export plumbing.

## 4. Utilities sub-contract (consumed via the same umbrella or directly)

The `utils` module is `core`-only and exposes (to other framework modules, not necessarily
to end consumers):

- `Stride::Row(width, format)` → `size_t` (video) / `Stride::Sample(...)`
  (audio)
- `MediaFileFormat` — shared file-extension constants (`kMp4`, `kH264`, ...)
  and `HasExtension(path, ext)`, so callers never hand-code suffix literals.

Pixel-format conversion moved OUT of `utils` into the libyuv-backed
`convert/libyuv` module — `PixelConverter::Convert(...)` → `Status`
(v1: `kI420 ↔ kNV12`). libyuv is a neutral cross-platform dependency, so it is
available everywhere libyuv links (unlike the FFmpeg-bound audio converter).

PCM sample-format conversion moved OUT of `utils` into the FFmpeg-backed
`backend/ffmpeg/swr` module (libswresample) — `SwrAudioConverter::Convert(...)`
→ `Status` (v1: `kS16`/`kF32` × interleaved/planar). It is only available
where FFmpeg is linked (non-Android).

All return the error model on unsupported input; none produce corrupt output.

## 5. Acceptance

- **A1**: A translation unit that `#include`s only `video_codec.h` compiles and links for
  both static and shared builds using exclusively public symbols.
- **A2**: Including any internal header path (e.g., `core/types.h` directly, or
  `backend/ffmpeg/...`) from outside the framework fails visibility / is not required.
- **A3**: Every supported `utils` conversion passes the round-trip / reference tests in
  `codec/tests/utils/`.
