#!/usr/bin/env bash
# Android cross-build of the MediaCodec spike. Invoked by `make android-verify`.
# Uses --config android_arm64, which applies the Android NDK cc_toolchain
# (rules_android_ndk) + --platforms + cc-toolchain resolution (see .bazelrc).
# Requires ANDROID_NDK_HOME (see specs/006-android-mediacodec-backend/quickstart.md).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

echo "[android] cross-build mediacodec_spike (android_arm64, NDK=$ANDROID_NDK_HOME)"
bazel build //src/spike:mediacodec_spike --config android_arm64
echo "[android] build OK"
