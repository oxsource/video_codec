# Implementation Plan: Core Utilities & Public API Surface

**Branch**: `003-core-utils-public-api` | **Date**: 2026-08-12 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/003-core-utils-public-api/spec.md`

## Summary

Close two still-unimplemented foundational gaps identified by the architecture design
(spec `002-architecture-engineering-design`): (1) the `utils` media-helper module — pixel
format conversion (`kI420`↔`kNV12`), stride computation, and PCM sample-format conversion —
which the FFmpeg/AAC backend and the future example depend on; and (2) the single public
umbrella header (`video_codec.h`) plus the existing `VIDEO_CODEC_API` export macro, which
makes the library consumable. The core types, error model, `LogSlot`, and abstract encoder
interfaces already ship (commit `628e5be`); this plan adds only the missing foundation
pieces (T008 + T026 in the spec-002 task list).

## Technical Context

**Language/Version**: C++17 (per spec `001-project-scaffold`)

**Build System**: Bazel 6.5.0

**Primary Dependencies**: `core` (existing types/error/logging); `googletest` (testing).
No new third-party dependency is introduced — `utils` stays `core`-only by architecture rule.
FFmpeg/libx264 remains a backend dependency (selected via `select()`), not a `utils` dependency.

**Storage**: N/A (library project, no persistent storage)

**Testing**: googletest — `codec/tests/utils/` for conversion/stride/PCM round-trip and a
header-only compile test for the public umbrella.

**Target Platform**: macOS ARM64 (development), Linux x86_64 (CI/release), Android arm64
(MediaCodec backend, cross-build)

**Project Type**: C++ static/shared library with a single public C++ include surface
(umbrella header + `VIDEO_CODEC_API` export macro)

**Performance Goals**: Conversions are buffer-to-buffer with no allocation on the expected
path; stride math is O(1). Hot encode path must not be surprised by extra copies.

**Constraints**: `utils` depends ONLY on `core` (architecture `module-dependencies.md`);
`public` is the ONLY `//visibility:public` module; error model (`StatusCode`/`Result<T>`)
crosses the public boundary — no exceptions. C++17 only.

**Scale/Scope**: Two small modules: `utils` (3 helper areas) and `public` (1 umbrella
header re-exporting existing contracts). No new runtime services.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Constitution file (`.specify/memory/constitution.md`) is the placeholder template only — no
project-specific principles, constraints, or gates defined.

- **Gate 1 — Project principles**: No binding principles defined. PASS.
- **Gate 2 — Constraints**: No binding constraints defined. PASS.
- **Gate 3 — Governance**: No governance rules defined. PASS.

**Verdict**: All gates pass. Constitution is a template awaiting project-specific content.
(Post-design re-check below confirms no violation introduced — the `utils`-only-`core`
constraint and single public-visibility rule are honored by this plan.)

## Project Structure

### Documentation (this feature)

```text
specs/003-core-utils-public-api/
├── spec.md              # Feature specification (/speckit.specify output)
├── plan.md              # This file (/speckit.plan output)
├── research.md          # Phase 0 output: conversion/export decisions
├── data-model.md        # Phase 1 output: conversion I/O + public re-export surface
├── quickstart.md        # Phase 1 output: consumer/developer quickstart
├── contracts/
│   └── public-api.md    # Phase 1 output: umbrella include + export contract
├── checklists/
│   └── requirements.md  # Spec quality checklist (all PASS)
└── tasks.md             # Phase 2 output (/speckit.tasks - NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
codec/src/framework/utils/          # existing BUILD.bazel (deps: core); add sources:
├── yuv_convert.h / .cc             # kI420 <-> kNV12 (R1)
├── stride.h / .cc                  # RowStride(width, PixelFormat) / SampleStride (R2)
└── pcm_convert.h / .cc             # kS16/kF32 x interleaved/planar (R3)

codec/src/framework/public/
├── include/video_codec/
│   ├── video_codec_export.h        # EXISTING: VIDEO_CODEC_API macro (reused, R4)
│   └── video_codec.h               # NEW: umbrella re-exporting public contracts (R4)
└── BUILD.bazel                     # EXISTING: globs *.h, //visibility:public; no change needed

codec/tests/utils/                  # NEW: conversion/stride/pcm unit tests + BUILD.bazel
```

**Structure Decision**: `utils` sources land in the already-stubbed `utils` package (its
BUILD globs `*.cc`/`*.h` and deps `core`, so no BUILD edit is required). The umbrella header
lands in the existing `public/include/video_codec/`; `public/BUILD.bazel` already globs that
directory and is `//visibility:public`, so adding `video_codec.h` needs no BUILD change. New
tests get a dedicated `tests/utils` package. No new top-level directories are introduced.

## Complexity Tracking

N/A — No constitution violations to justify. The design honors the existing
`utils`-only-`core` and single-public-visibility constraints; no extra modules or patterns
are added.
