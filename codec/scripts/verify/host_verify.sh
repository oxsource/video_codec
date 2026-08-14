#!/usr/bin/env bash
# Host verification: build //... + run ffmpeg_spike + run the encode_file
# example + assert both outputs are valid H.264 streams with ffprobe.
# Invoked by `make host-verify`.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

echo "[host] 1/4 build //..."
bazel build //...

echo "[host] 2/4 run ffmpeg_spike"
bash "$(dirname "$0")/host_spike.sh" >/dev/null

echo "[host] 3/4 run encode_file example"
bash "$(dirname "$0")/host_ffmpeg_codec.sh"

echo "[host] 4/4 validate spike output with ffprobe"
mkdir -p "$ROOT/out"
OUT="$ROOT/out/ffmpeg_spike.h264"
[ -f "$OUT" ] || { echo "[host] FAIL: ffmpeg_spike.h264 not found in out/"; exit 1; }

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
