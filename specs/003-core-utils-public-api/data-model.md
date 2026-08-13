# Data Model: Core Utilities & Public API Surface (spec 003)

**Branch**: `003-core-utils-public-api` | **Date**: 2026-08-12
**Feature**: [spec.md](spec.md) | **Research**: [research.md](research.md)

This feature adds no persistent data entities — it defines a **function/header surface**.
The "data model" is therefore the set of conversion I/O shapes and the public re-export
surface, built on the core types already defined in `core/types.h` (commit `628e5be`).

## 1. Conversion I/O shapes (utils module)

All helpers operate on caller-owned buffers and report failures via the existing error
model (`Status` / `Result<T>`). They never allocate the primary payload internally on
the expected path.

### 1.1 Pixel-format conversion
- **Input**: source `VideoFrame` (or raw plane pointers + `PixelFormat` + width/height +
  per-format strides), destination `PixelFormat` (`kI420` or `kNV12`).
- **Output**: destination planes filled; `Status::kOk` on success, or an error status
  for unsupported/unaligned input.
- **Relationship**: consumes `PixelFormat`, `VideoFrame` from `core`; produces no new types.

### 1.2 Stride helper
- **Input**: `width`, `PixelFormat` (video) or `SampleFormat` (audio).
- **Output**: `size_t` bytes-per-row (video) or bytes-per-frame-element (audio).
- **Validation**: width > 0; otherwise error status.

### 1.3 PCM sample-format conversion
- **Input**: source audio buffer + `SampleFormat` (one of `kS16`, `kF32`, `kS16Planar`,
  `kF32Planar`) + channel count + sample count.
- **Output**: converted buffer in the requested `SampleFormat`; error status on unsupported
  pair.
- **Relationship**: consumes `SampleFormat`, `AudioFrame` from `core`.

## 2. Public re-export surface (umbrella header)

The umbrella `video_codec.h` re-exports exactly the frozen public contracts. It introduces
no new types; it is a forwarding include surface.

| Re-exported header (from `core` / `api`) | Exposes |
|------------------------------------------|---------|
| `core/types.h` | `VideoCodecType`, `AudioCodecType`, `PixelFormat`, `SampleFormat`, `Backend`, `VideoFrame`, `AudioFrame`, `Packet`, `NativeBuffer`, `*EncoderConfig` |
| `core/status.h`, `core/result.h` | `Status`, `Result<T>` |
| `core/log_slot.h` | `LogSlot` (pluggable logging interface) |
| `api/video_encoder.h`, `api/audio_encoder.h` | `VideoEncoder`, `AudioEncoder` (abstract) |
| `api/input_surface.h` | `InputSurface` / `NativeBuffer` semantics |
| `api/encoder_factory.h` | factory + backend selection |

The umbrella is decorated with `VIDEO_CODEC_API` (from `video_codec_export.h`) so public
symbols export correctly for shared-library builds.

## 3. Validation rules (from spec requirements)

- Unsupported conversion pair → error status, never corrupt output (FR-005).
- Stride for width not a multiple of natural alignment → returns the padded layout the
  backend expects; caller must honor it (edge case in spec).
- Public surface → only the intended contracts; internal modules unreachable (FR-007).
- Consumer compiles against the library using only `video_codec.h` (FR-009).

## 4. State transitions

None at the data level. (Encoder lifecycle state machine already lives in `api` and is not
redefined here.)
