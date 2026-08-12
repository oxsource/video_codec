#!/usr/bin/env bash
# Android cross-build of the MediaCodec spike. Invoked by `make android-build`.
# Requires the deferred `android_ndk_repository(name = "androidndk")` registration
# in WORKSPACE (see tasks.md T022/T023) before it links; until then bazel reports
# the missing repo.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

echo "[android] cross-build mediacodec_spike (android_arm64)"
echo "[android] NOTE: needs android_ndk_repository(name=androidndk) in WORKSPACE (deferred)"
bazel build //src/spike:mediacodec_spike \
  --platforms=//platforms:android_arm64_platform \
  --android_platforms=//platforms:android_arm64_platform
echo "[android] build OK"
