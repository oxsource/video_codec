#!/usr/bin/env bash
# Host spike: run the FFmpeg libx264 encode spike (no full rebuild).
# The spike writes ffmpeg_spike.h264 to bazel run's CWD (runfiles dir); move it
# into the shared test-output directory. Invoked by `make host-spike`.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

echo "[host] bazel run //src/spike:ffmpeg_spike"
bazel run //src/spike:ffmpeg_spike

mkdir -p "$ROOT/out"
SPIKE="$(find -L bazel-bin -name ffmpeg_spike.h264 2>/dev/null | head -1)"
if [ -n "$SPIKE" ]; then
  mv -f "$SPIKE" "$ROOT/out/ffmpeg_spike.h264"
  echo "[host] spike output -> out/ffmpeg_spike.h264"
fi
