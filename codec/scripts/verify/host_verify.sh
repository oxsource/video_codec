#!/usr/bin/env bash
# Host verification: build //... + run ffmpeg_spike + assert the output is a
# valid 320x240 H.264 stream with ffprobe. Invoked by `make host-verify`.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

echo "[host] 1/3 build //..."
bazel build //...

echo "[host] 2/3 run ffmpeg_spike"
bazel run //src/spike:ffmpeg_spike >/dev/null

echo "[host] 3/3 validate output with ffprobe"
OUT="$(find -L bazel-bin -name ffmpeg_spike.h264 2>/dev/null | head -1)"
[ -n "$OUT" ] || { echo "[host] FAIL: ffmpeg_spike.h264 not found"; exit 1; }

SIZE="$(wc -c < "$OUT" | tr -d ' ')"
INFO="$(ffprobe -v error -show_entries stream=codec_name,width,height \
        -of default=noprint_wrappers=1 "$OUT")"
echo "$INFO"
echo "size=$SIZE bytes"

echo "$INFO" | grep -qx 'codec_name=h264' || { echo '[host] FAIL: not h264'; exit 1; }
echo "$INFO" | grep -qx 'width=320'        || { echo '[host] FAIL: width != 320'; exit 1; }
echo "$INFO" | grep -qx 'height=240'       || { echo '[host] FAIL: height != 240'; exit 1; }

if [ "$SIZE" -ne 14778 ]; then
  echo "[host] WARN: size $SIZE != 14778 (libx264 build drift) — stream still valid"
fi
echo "[host] PASS: valid 320x240 H.264 ($SIZE bytes)"
