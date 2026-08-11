# Feature Specification: Project Scaffolding (video_codec)

**Version**: 0.1
**Status**: Draft
**Source**: `codec/doc/project_bootstrap.md`
**Purpose**: Establish the Bazel build system, platform definitions, dependency
management, and module skeleton for the cross-platform audio/video encoding
framework, and validate the highest-risk integration (FFmpeg encoding) via a spike.

---

## 1. Goal

Stand up a buildable, cross-platform Bazel workspace that reflects the module
structure defined in `project_bootstrap.md`, with all third-party dependencies
wired (FFmpeg via `http_archive`, Android NDK via toolchain), and prove that the
primary encoding backend (FFmpeg) compiles, links, and produces a valid bitstream
before any downstream encoder code is written.

This phase carries the highest technical risk: FFmpeg's `libavcodec` build system
is large and its Bazel integration requires care; Android NDK wiring for MediaCodec
must be correct even though it can only be fully exercised on-device/CI.

## 2. Functional Requirements

### FR-001 Bazel Workspace Root at `codec/`

The Bazel workspace root is `codec/` (not the git repo root `video_codec/`).
It must contain `WORKSPACE`, `BUILD.bazel` (root alias), `.bazelversion` (6.5.0),
`.bazelrc`, `video_codec_deps.bzl`.

### FR-002 Platform Definitions

`platforms/` must define `android_arm64`, `linux_x86_64`, `darwin_arm64` via a
`config_setting_and_platform` macro, with `.bazelrc` aliases mapping each to a
`--platforms` flag.

### FR-003 Dependency Management

All external deps declared in `video_codec_deps.bzl` via a single
`video_codec_setup()` call, guarded with `native.existing_rule()`:
- FFmpeg 6.1 (`http_archive` + `third_party/ffmpeg/BUILD.bazel` wrapper)
- googletest 1.14.0
- bazel_skylib 1.6.1
- Android NDK wired via `android_ndk_repository` / `@androidndk` (not http_archive)

### FR-004 Module Skeleton

Create empty `cc_library` stubs for each module under `codec/src/framework/`:
`core`, `api`, `backend/android`, `backend/ffmpeg`, `utils`, `public`.
Each carries `BUILD.bazel` with `__subpackages__` visibility; only `public` is
`//visibility:public`. The `public` target aggregates all modules and `select()`s
the platform backend.

> Note: `backend/darwin` (VideoToolbox) is reserved but NOT created in P1 — deferred
> to Phase 2+ per `project_bootstrap.md`.

### FR-005 FFmpeg Encode Spike

`codec/src/spike/ffmpeg_spike.cc` builds a minimal binary that:
- Allocates an `AVCodecContext` for `libx264`
- Feeds a synthetic NV12 `AVFrame`
- Encodes to an `AVPacket` and writes Annex-B bytes to a `.h264` file

Success = compiles, links, runs, and produces a non-empty, decodable bitstream.
This spike runs on Linux x86_64 and macOS ARM64 (development hosts).

### FR-006 MediaCodec Build Spike (Android-only)

`codec/src/spike/mediacodec_spike.cc` exercises `AMediaCodec` configuration for
`video/avc`. Because NDK headers/libs are unavailable on dev hosts, this target is
marked `target_compatible_with = ["@platforms//os:android"]` and validated by
building under the Android toolchain / CI. It must not break the default host build.

### FR-007 Code Style & Conventions

Adopt Google C++ Style (2-space indent, 80-col) and Conventional Commits, mirrored
from `project_bootstrap.md` §5–§6.

## 3. Non-Functional Requirements

- **Reproducible**: `.bazelversion` pins exact Bazel; FFmpeg pinned by sha256.
- **Isolated deps**: No module outside `backend/ffmpeg` may depend on `@ffmpeg`
  directly; no module outside `backend/android` may depend on NDK media headers.
- **Cross-platform build**: Default dev build (Linux/macOS) must succeed without
  Android NDK present.

## 4. Success Criteria

- `bazel build //...` succeeds on Linux x86_64 and macOS ARM64
- `ffmpeg_spike` produces a valid `.h264` file
- `mediacodec_spike` builds under the Android toolchain (CI) without breaking host build
- All module `BUILD.bazel` stubs present and compilable
- Developer can run `bazel run //src/spike:ffmpeg_spike` to validate the pipeline
