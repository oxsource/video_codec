# Research: Project Scaffolding (video_codec)

**Date**: 2026-08-11

## Overview

All technical decisions for P1 are documented in `codec/doc/project_bootstrap.md`.
No NEEDS CLARIFICATION items existed in the spec — the design doc already pins
build system, language, dependencies, module layout, and backend selection. This
document consolidates the decisions, rationale, and alternatives for the
scaffolding phase, mirroring the structure of `native_ui/specs/001-project-scaffold`.

---

## Decision Log

### Build System: Bazel 6.5.0

- **Decision**: Use Bazel 6.5.0 as the build system
- **Rationale**: Required for reproducible multi-platform builds; same toolchain as
  `native_ui` / `graph_runtime` (sibling projects), reducing team context switching.
  `select()` is essential for switching backend implementations per platform.
- **Alternatives considered**: CMake (rejected: weaker dependency management, no
  sandboxing); Make (rejected: non-reproducible, no multi-platform support)

### Workspace Root: `codec/`

- **Decision**: Bazel workspace lives at `codec/WORKSPACE`, not the git repo root
- **Rationale**: `project_bootstrap.md` defines `codec/` as the workspace root so the
  encoding library is a self-contained package; `specs/` stays at the git repo root
  alongside the project for spec-kit tooling.
- **Alternatives considered**: Workspace at repo root (rejected: mixes spec/tooling
  dirs with the library package)

### Platform Targets: Android ARM64 + Linux x86_64 + macOS ARM64

- **Decision**: Android ARM64 and Linux x86_64 are primary (the two implemented
  backends); macOS ARM64 is the development host and reserved for the future
  VideoToolbox backend.
- **Rationale**: Android MediaCodec + generic FFmpeg cover all immediate targets.
  macOS is the dev machine; VideoToolbox is deferred to Phase 2+.
- **Alternatives considered**: Windows (deferred: not in P1 scope)

### Language Standard: C++17

- **Decision**: Use C++17 with `-fvisibility=hidden`
- **Rationale**: Matches `native_ui` / `graph_runtime`; sufficient for the encoder
  abstractions; broad toolchain support including Android NDK.
- **Alternatives considered**: C++20 (rejected: weaker NDK support)

### FFmpeg Integration: http_archive + BUILD wrapper

- **Decision**: Fetch FFmpeg 6.1 via `http_archive`, provide a `cc_library` wrapper
  in `third_party/ffmpeg/BUILD.bazel` exposing `libavcodec` / `libavutil`
- **Rationale**: Reproducible; wrapper pins exactly the sources/headers needed and
  centralizes platform `linkopts`. Avoids submodule maintenance.
- **Alternatives considered**: Git submodule (rejected: version overhead); system
  FFmpeg (rejected: non-reproducible across platforms)

### Android NDK Integration: android_ndk_repository

- **Decision**: Wire NDK via `android_ndk_repository` / `@androidndk` (Bazel native),
  not `http_archive`. A thin `third_party/android_ndk/BUILD.bazel` wrapper exposes
  `media/NdkMediaCodec.h` for non-Android builds that still reference the headers in
  tests.
- **Rationale**: NDK ships with the Android toolchain; `http_archive` would duplicate
  a large, platform-specific download.
- **Alternatives considered**: `http_archive` of NDK (rejected: redundant, large)

### Dependency Management: Single deps.bzl

- **Decision**: Centralize external deps in `video_codec_deps.bzl` with one
  `video_codec_setup()` call, guarded by `native.existing_rule()`
- **Rationale**: Single source of truth; easy audit. Mirrors `native_ui_deps.bzl`.
- **Alternatives considered**: Inline in WORKSPACE (rejected: cluttered)

### Testing Framework: googletest

- **Decision**: googletest 1.14.0 via `http_archive`
- **Rationale**: Industry standard, good Bazel integration, matches sibling projects.
- **Alternatives considered**: Catch2 (rejected: weaker Bazel ecosystem fit)

### Module Visibility: __subpackages__ isolation

- **Decision**: Each module `cc_library` uses
  `visibility = ["//src/framework:__subpackages__", "//tests:__subpackages__"]`;
  only `public/` is `//visibility:public`
- **Rationale**: Encapsulation; enforces that `@ffmpeg` is reachable only via
  `backend/ffmpeg` and NDK media only via `backend/android`.
- **Alternatives considered**: All-public (rejected: no encapsulation)

### Spikes: ffmpeg_spike (runnable) + mediacodec_spike (Android-only build)

- **Decision**: `ffmpeg_spike.cc` encodes a synthetic NV12 frame to `.h264` and runs
  on dev hosts; `mediacodec_spike.cc` validates `AMediaCodec` config and is gated to
  Android (`target_compatible_with`) so it never breaks the host build.
- **Rationale**: FFmpeg is the highest-risk, runnable-on-host dependency and must be
  proved end-to-end. MediaCodec cannot run on macOS/Linux hosts, but its build must
  be proven so P2 backend work starts from a compiling base.
- **Alternatives considered**: Skip MediaCodec spike (rejected: NDK wiring is risky
  and must at least compile); compile-only FFmpeg test (rejected: doesn't prove encode)

---

## Risk Assessment

| Risk | Mitigation | Status |
|------|------------|--------|
| FFmpeg `http_archive` BUILD wrapper incomplete | `ffmpeg_spike` validates compile+link+run before P2 | Active |
| Android NDK wiring incorrect | `mediacodec_spike` builds under Android toolchain / CI | Active |
| Host build pulls NDK accidentally | NDK dep only in `backend/android`; `select()` excludes on host | Mitigated |
| Bazel version drift | `.bazelversion` pins 6.5.0 | Mitigated |
| Large initial dependency download | Documented as expected (FFmpeg ~minutes) | Accepted |
