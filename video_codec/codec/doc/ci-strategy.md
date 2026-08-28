# CI Strategy

GitHub Actions. Three jobs; all run on every push and PR.

## Jobs

### `macos` (development reference)
- Runner: `macos-14` (ARM64).
- Steps: `bazel build //...` → `bazel test //...` → clang-format → clang-tidy.
- Validates the dev-host path end-to-end (FFmpeg source build + ffmpeg_spike golden).

### `linux` (release/build reference)
- Runner: `ubuntu-22.04` (x86_64).
- Same steps as `macos`. Proves the build is reproducible off-macOS and on x86_64.

### `android` (NDK cross-build gate)
- Runner: `ubuntu-22.04`.
- Requires `android_ndk_repository` registered in `WORKSPACE` (research.md R2).
- Steps: `bazel build //src/spike:mediacodec_spike --platforms=//platforms:android_arm64_platform`.
- Cheap gate that catches NDK/toolchain breakage without a device.

## Gates

| Gate | Tool | Fail condition |
|------|------|----------------|
| Build | `bazel build //...` | any target fails |
| Test | `bazel test //...` | any test fails |
| Format | `clang-format --dry-run` | diff present |
| Lint | `clang-tidy` | warning/error |

## Caching

Bazel's local + remote cache (or `--disk_cache`) is recommended to avoid rebuilding
FFmpeg 6.1 from source each run. The FFmpeg `configure_make` output is cacheable.

## Local equivalent

`make verify` (host) and `make build-android` reproduce the `macos` and `android` jobs
locally. See `codec/mk/` and `scripts/verify/`.
