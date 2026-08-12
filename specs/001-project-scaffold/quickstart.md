# Quickstart: Building & Validating video_codec (Scaffold Phase)

**Date**: 2026-08-11

## Prerequisites

- Bazel 6.5.0 (pinned via `codec/.bazelversion`; install with `bazelisk`)
- FFmpeg 6.1 **built from source** via `rules_foreign_cc` on first build (the release tarball is fetched by `http_archive`, then `third_party/ffmpeg/BUILD.bazel` compiles it)
- Android NDK (only required to build `mediacodec_spike` / Android targets)

## Build (host: Linux x86_64 or macOS ARM64)

```bash
cd codec
bazel build //...                 # builds all modules + spikes
```

This compiles the `core`, `api`, `utils`, `backend/ffmpeg`, `backend/android`
stubs and both spikes. The Android backend stub compiles on host because NDK
headers are referenced only under the Android toolchain; on host it is excluded by
`select()`.

> **Scope note:** `examples/` encode demos and `backend/darwin` (VideoToolbox) are
> **not** part of this scaffold phase — both are deferred to the Phase 2
> implementation. The scaffold validates the FFmpeg path via `ffmpeg_spike` and the
> MediaCodec path via `mediacodec_spike` (Android-only).

## Run the FFmpeg validation spike

```bash
bazel run //src/spike:ffmpeg_spike
# writes ffmpeg_spike.h264 into the runfiles dir (a real libx264-encoded
# 320x240 H.264/Annex-B stream, 14778 bytes), NOT codec/bazel-out/...
```

Validate the output is a real H.264 stream:

```bash
ffprobe ffmpeg_spike.h264         # or: ffplay ffmpeg_spike.h264
```

## Build the MediaCodec spike (Android only)

```bash
bazel build //src/spike:mediacodec_spike \
  --platforms=//platforms:android_arm64_platform \
  --android_platforms=//platforms:android_arm64_platform
```

On a non-Android host this target is skipped (`target_compatible_with`), so the
default `bazel build //...` never requires the NDK.

## Test

```bash
bazel test //...
```

## Quick validation (make)

The repo ships an AOSP-style `Makefile` at the workspace root (`codec/Makefile`)
that categorizes validation into self-describing modules under `codec/mk/` and
delegates the real work to `codec/scripts/verify/*.sh`. Each category owns
prefixed targets (`host-*`, `android-*`, `docs-*`) so they never clash; friendly
short aliases live in `codec/mk/aliases.mk`. Run from the workspace root:

```bash
make              # list all targets (same as `make help`)
make modules      # list registered categories (host / android / docs)
make build        # host-build:   bazel build //...
make spike        # host-spike:   run the FFmpeg encode spike only
make verify       # host-verify:  build + run spike + ffprobe assert (320x240 h264)
make docs         # docs-check:   quick doc/layout consistency (no build)
make build-android  # android-build: cross-build mediacodec_spike (needs NDK, deferred)
make verify-android # alias of android-build
```

`make verify` is the one-command scaffold proof: it builds everything, runs the
FFmpeg libx264 spike, and asserts the output is a valid 320x240 H.264 stream with
`ffprobe`. `make build-android` / `make verify-android` require the deferred
`android_ndk_repository(name = "androidndk")` registration in `WORKSPACE` (see T022/T023).

To add a category, drop a new `codec/mk/<name>.mk` that calls
`$(call register_module,<name>)` and declares its `<name>-<action>` targets —
it is picked up automatically. Duplicate module/target/alias names abort the build.

## Common issues

| Symptom | Fix |
|---------|-----|
| `ffmpeg` sha256 mismatch | Update `video_codec_deps.bzl` with the pinned 6.1 sha256 |
| NDK not found on host | Expected — NDK is Android-only; don't build `mediacodec_spike` on host |
| `select()` picks wrong backend | Check `--platforms`; `.bazelrc` has no forced default, so Bazel uses the host platform automatically (macOS dev → darwin_arm64, Linux CI → linux_x86_64) |
