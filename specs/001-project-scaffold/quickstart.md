# Quickstart: Building & Validating video_codec (Scaffold Phase)

**Date**: 2026-08-11

## Prerequisites

- Bazel 6.5.0 (pinned via `codec/.bazelversion`; install with `bazelisk`)
- FFmpeg fetched automatically via `http_archive` on first build
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

## Run the FFmpeg validation spike

```bash
bazel run //src/spike:ffmpeg_spike
# writes codec/bazel-out/.../ffmpeg_spike.h264 (synthetic encoded stream)
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

## Common issues

| Symptom | Fix |
|---------|-----|
| `ffmpeg` sha256 mismatch | Update `video_codec_deps.bzl` with the pinned 6.1 sha256 |
| NDK not found on host | Expected — NDK is Android-only; don't build `mediacodec_spike` on host |
| `select()` picks wrong backend | Check `--platforms`; `linux_x86_64` default in `.bazelrc` |
