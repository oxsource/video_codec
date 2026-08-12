#!/usr/bin/env bash
# Host spike: run the FFmpeg libx264 encode spike (no full rebuild).
# Invoked by `make host-spike`.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

echo "[host] bazel run //src/spike:ffmpeg_spike"
bazel run //src/spike:ffmpeg_spike
