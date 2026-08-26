#!/usr/bin/env bash
# Android arm64 cross-build of the stream core + mock backend. Invoked by
# `make android-verify`. Uses --config android_arm64, which applies the Android
# NDK cc_toolchain (rules_android_ndk) + --platforms + cc-toolchain resolution
# (see .bazelrc). Requires ANDROID_NDK_HOME.
#
# Mirrors codec's android build: cross-compiles the platform-independent core
# (no openssl/libdatachannel/curl, which are host-only for now).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

echo "[android] cross-build stream_core + mock_backend (android_arm64, NDK=$ANDROID_NDK_HOME)"
bazel build //src/core:stream_core //src/backend/mock:mock_backend --config android_arm64
echo "[android] build OK"
