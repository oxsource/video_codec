#!/usr/bin/env bash
# Full host validation: build the stream library + example binary.
# Invoked by `make host-verify`.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

echo "[host] bazel build //..."
bazel build //...

echo "[host] bazel build encode_and_push example"
bazel build //src/examples:encode_and_push

echo "[host] verify OK"
