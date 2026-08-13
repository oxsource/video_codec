# Quickstart: Architecture & Engineering Design (scaffold → framework dev)

**Date**: 2026-08-12 | **Branch**: `002-architecture-engineering-design`

This quickstart is for developers working on the video_codec framework after the
scaffold phase. It assumes `001-project-scaffold` is merged and the workspace builds.

## Prerequisites

- Bazel 6.5.0 (`codec/.bazelversion`; use `bazelisk`)
- FFmpeg 6.1 built from source via `rules_foreign_cc` (happens on first `bazel build`)
- Android NDK **only** to build/run `mediacodec_spike` or `backend/android`
  (register `android_ndk_repository(name = "androidndk")` in `WORKSPACE` — see
  `research.md` R2)
- `ffprobe` (for H.264 golden checks) and `clang-format` / `clang-tidy` (for CI gates)

## Build & validate

From the workspace root (`codec/`):

```bash
cd codec

# One-command scaffold/design proof (build + run ffmpeg_spike + ffprobe assert)
make verify

# Or step by step:
make build         # bazel build //...
make spike         # run the FFmpeg encode spike only
make docs          # quick doc/layout consistency (no build)

# Android cross-build (needs NDK registered):
make build-android
```

All `make` targets are categorized modules under `codec/mk/` — see
`codec/Makefile` and the `host` / `android` / `docs` modules.

## Run tests

```bash
bazel test //...    # unit (core/utils) + integration (backends) + smoke (spikes)
```

After the implementation phase this also runs the backend integration tests that
encode a few frames and assert the output decodes (`codec/doc/testing-strategy.md`).

## Project layout

```text
codec/
├── doc/
│   ├── project_bootstrap.md     # Original design vision (Phase 1)
│   ├── architecture/             # << this phase's architecture docs
│   │   ├── README.md
│   │   ├── module-dependencies.md
│   │   ├── error-handling.md
│   │   ├── lifecycle-model.md
│   │   ├── backend-selection.md
│   │   ├── threading.md
│   │   └── logging-slot.md
│   ├── adrs/                     # << Architecture Decision Records
│   ├── build-conventions.md
│   ├── testing-strategy.md
│   ├── ci-strategy.md
│   └── release-process.md
├── src/framework/
│   ├── core/  api/  utils/
│   ├── backend/{android,ffmpeg,darwin}/   # each its own BUILD.bazel
│   └── public/
├── third_party/{ffmpeg,android_ndk}/
└── mk/ + scripts/verify/          # make validation mechanism
```

## Adding a backend (contract-first)

1. Read `specs/002-.../contracts/encoder-contract.md` and `backend-contract.md`.
2. Create `src/framework/backend/<name>/` with its `BUILD.bazel` (visibility
   `__subpackages__` + `tests`; depends on `api`/`core`/`utils` + its external dep).
3. Subclass `VideoEncoder`/`AudioEncoder`; implement `Init` / `Encode`×2 / `Flush` /
   `Release`; map errors to `Status` (no exceptions).
4. Wire it into the `select()` list in `src/framework/public/BUILD.bazel` and the
   factory in `api/encoder_factory.cc` (see `codec/doc/architecture/backend-selection.md`).
5. Add an integration test under `tests/`; run `make verify` + `bazel test //...`.

## Reading the design

Start at `codec/doc/architecture/README.md` (index + ADR list). The load-bearing
decisions (static force-loaded FFmpeg, `select()`-per-platform, BSD `libtool` merge,
deferred VideoToolbox) are in `codec/doc/adrs/`.
