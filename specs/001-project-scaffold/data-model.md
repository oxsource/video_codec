# Data Model: Project Scaffolding (video_codec)

**Date**: 2026-08-11

## Entities

### Bazel Workspace

Top-level container for the encoding library. Identified by
`workspace(name = "video_codec")` at `codec/WORKSPACE`. All source paths are
relative to `codec/`.

- **name**: `video_codec`
- **root**: `<repo_root>/codec/`
- **conventions**: Mirrors `native_ui` / `graph_runtime` layout

### Module

A logical source grouping under `codec/src/framework/`. Each is a Bazel package.

| Module | Path | Visibility | Dependencies |
|--------|------|------------|--------------|
| core | `src/framework/core` | `__subpackages__`, `tests` | None |
| api | `src/framework/api` | `__subpackages__`, `tests` | core |
| utils | `src/framework/utils` | `__subpackages__`, `tests` | core |
| backend/android | `src/framework/backend/android` | `__subpackages__`, `tests` | core, api, utils, `@androidndk` |
| backend/ffmpeg | `src/framework/backend/ffmpeg` | `__subpackages__`, `tests` | core, api, utils, `@ffmpeg` |
| public | `src/framework/public` | `//visibility:public` | core, api, utils, `select()` backend |

> `backend/darwin` is reserved for Phase 2+ and NOT created in P1.

### Platform

A target environment defined by OS + CPU.

- **android_arm64**: `@platforms//os:android`, `@platforms//cpu:aarch64`
- **linux_x86_64**: `@platforms//os:linux`, `@platforms//cpu:x86_64`
- **darwin_arm64**: `@platforms//os:macos`, `@platforms//cpu:aarch64`

### Build Target

A named buildable unit (`cc_library`, `cc_binary`, `cc_test`) with deps,
visibility, and optional platform constraints.

- **type**: library / binary / test
- **visibility**: package access control
- **target_compatible_with**: optional platform gate (used by `mediacodec_spike`)

### Third-Party Dependency

| Name | Type | Source | Purpose |
|------|------|--------|---------|
| ffmpeg | http_archive | ffmpeg.org 6.1 | Audio/video encode (`libavcodec`) |
| googletest | http_archive | GitHub 1.14.0 | Unit testing |
| bazel_skylib | http_archive | GitHub 1.6.1 | Build helpers |
| androidndk | android_ndk_repository | local/CI toolchain | MediaCodec (`AMediaCodec`) |

### Spike Binary

Minimal executable validating a high-risk integration compiles, links, runs.

- **ffmpeg_spike**: `src/spike/ffmpeg_spike.cc`; deps `@ffmpeg`; runs on host;
  success = non-empty decodable `.h264`
- **mediacodec_spike**: `src/spike/mediacodec_spike.cc`; deps `@androidndk`;
  `target_compatible_with = ["@platforms//os:android"]`; success = builds under
  Android toolchain

## State Transitions

N/A — P1 is a build-system scaffolding phase. No runtime state transitions.

## Validation Rules

- **Visibility Rule**: No target outside `backend/ffmpeg` may depend on `@ffmpeg`;
  no target outside `backend/android` may depend on NDK media headers.
- **Platform Rule**: `select()` must link only the matching backend;
  NDK deps must never reach the host (Linux/macOS) build.
- **Dep Rule**: All external deps declared in `video_codec_deps.bzl` with
  `native.existing_rule()` guards.
- **Stub Rule**: All listed module `BUILD.bazel` files present and compilable.
- **Spike Rule**: `ffmpeg_spike` must produce a valid bitstream; `mediacodec_spike`
  must be Android-compatible and not break the default host build.
