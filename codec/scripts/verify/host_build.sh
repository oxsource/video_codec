#!/usr/bin/env bash
# Host build: compile every target (compiles FFmpeg 6.1 from source via
# rules_foreign_cc). Invoked by `make host-build`.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

echo "[host] bazel build //..."
bazel build //...
echo "[host] build OK"
