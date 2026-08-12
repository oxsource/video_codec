# Research: Core Utilities & Public API Surface (spec 003)

**Branch**: `003-core-utils-public-api` | **Date**: 2026-08-12
**Feature**: [spec.md](spec.md)

This feature fills two still-unimplemented foundational gaps from the architecture design
(spec `002-architecture-engineering-design`): the `utils` module (T008) and the public
umbrella / API export surface (T026). The core types, error model, `LogSlot`, and abstract
encoder interfaces already exist (commit `628e5be`), so this research only resolves the
open technical questions for these two additions.

## R1 — Pixel-format conversion approach (YUV420P ↔ NV12)

- **Decision**: Hand-roll minimal, dependency-free converters inside `utils`. No `libyuv` /
  `libswscale` dependency.
- **Rationale**: The architecture rule (`module-dependencies.md`) requires `utils` to depend
  **only** on `core`; pulling FFmpeg (`swscale`) or an external lib would violate that and
  couple the foundation to a backend dependency. The only required v1 conversions are
  `kI420` (planar YUV420P) ↔ `kNV12` (semi-planar), which are simple, well-understood byte
  shuffles with deterministic stride handling.
- **Alternatives considered**:
  - `libswscale` (FFmpeg): richest, but adds a hard FFmpeg dependency to `utils` — rejected.
  - `libyuv`: small and fast, but an extra third-party dep for two trivial conversions —
    rejected for v1; can be adopted later behind the same `utils` interface if more formats
    are needed.
- **Scope guard**: Only `kI420 ↔ kNV12` for v1. `kRGBA` is included in the enum but
  RGB↔YUV conversion is out of scope (documented as a future extension).

## R2 — Stride computation

- **Decision**: A `utils` helper computes per-row byte stride from width + `PixelFormat`
  (and width + `SampleFormat` for audio), returning the padded layout the encoder backend
  expects. Conversions take explicit stride parameters so they never assume tight packing.
- **Rationale**: FFmpeg/`libx264` and `AMediaCodec` require properly strided planes; a single
  shared helper prevents each backend from reinventing alignment math.
- **Alternatives considered**: Per-backend inline math — rejected (divergence / bugs).

## R3 — PCM sample-format conversion

- **Decision**: Hand-rolled converters among the `SampleFormat` enum values present in
  `core/types.h`: `kS16`, `kF32`, `kS16Planar`, `kF32Planar`. Primarily `kS16` (interleaved)
  ↔ `kF32Planar` (what the FFmpeg AAC encoder consumes).
- **Rationale**: Same dependency-free argument as R1; the set is small and fixed for v1.
- **Scope guard**: Packed↔planar and s16↔f32 only; resampling / channel remix out of scope.

## R4 — Public umbrella header & export macro

- **Decision**: Reuse the existing `video_codec_export.h` (`VIDEO_CODEC_API` macro). Add a new
  umbrella header `public/include/video_codec/video_codec.h` that `#include`s the public
  contracts — the abstract encoder interfaces (`api/video_encoder.h`, `audio_encoder.h`),
  the core types/error/logging (`core/types.h`, `status.h`, `result.h`, `log_slot.h`), and
  `input_surface.h`. The existing `public/BUILD.bazel` already globs `include/video_codec/*.h`
  and is `//visibility:public`, so adding the header needs no BUILD change.
- **Rationale**: The export macro and the public Bazel target already exist; this task is
  purely "assemble the single include surface". Default build is static (macro expands to
  nothing); `-DVIDEO_CODEC_SHARED_LIBRARY` enables symbol export for shared builds (already
  wired in `public/BUILD.bazel` copts).
- **Alternatives considered**: Per-header public includes — rejected; a single umbrella is
  the documented contract (`contracts/public-api.md` in spec 002).

## R5 — Testing strategy

- **Decision**: googletest unit tests in `codec/tests/utils/` covering (a) round-trip
  bit-exactness for `kI420↔kNV12`, (b) stride correctness against hand-computed references,
  (c) PCM round-trip/sanity for the supported conversions, (d) a header-only compile test
  that includes only `video_codec.h` and calls a public factory entry point.
- **Rationale**: Matches the project testing strategy (`codec/doc/testing-strategy.md`) and
  the spec's success criteria (SC-001..SC-004).

## Open items resolved

All NEEDS CLARIFICATION from the technical context are resolved above. No external library
is introduced; `utils` remains `core`-only; the public surface reuses existing export
infrastructure.
