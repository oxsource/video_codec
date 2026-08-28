#!/usr/bin/env bash
# Android arm64 cross-build of stream core + mock backend and codec spike.
# Invoked by `make android-verify`. Requires ANDROID_NDK_HOME.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

echo "[android] cross-build stream_core + mock_backend (android_arm64, NDK=$ANDROID_NDK_HOME) $@"
bazel build //stream/src/core:stream_core //stream/src/backend/mock:mock_backend --config android_arm64 "$@"
echo "[android] stream build OK"

echo "[android] cross-build mediacodec_spike (android_arm64)"
bazel build //codec/src/spike:mediacodec_spike --config android_arm64
echo "[android] codec build OK"
