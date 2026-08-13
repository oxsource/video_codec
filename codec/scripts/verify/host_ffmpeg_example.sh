#!/usr/bin/env bash
# Host example: build + run the ffmpeg_encode_file demo (SMPTE color bars ->
# MP4) and assert the output is a valid 640x480 MP4 (H.264) with ffprobe.
# Invoked by `make host_ffmpeg_example` (and as part of `make host-verify`).
#
# Args: [seconds] — clip duration (default 2, keeps CI fast; the demo default is 5).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

DURATION="${1:-2}"

echo "[host] bazel build //src/examples:ffmpeg_encode_file"
bazel build //src/examples:ffmpeg_encode_file

BIN="$(find -L bazel-bin -path '*/examples/ffmpeg_encode_file' -type f | head -1)"
[ -n "$BIN" ] || { echo "[host] FAIL: ffmpeg_encode_file binary not found"; exit 1; }

mkdir -p "$ROOT/out"          # shared test-output directory (gitignored)
# The example appends the mode's extension (.mp4 by default), so pass a bare
# path and verify the .mp4 file it produces.
OUT_BASE="$ROOT/out/example_out"
OUT="$OUT_BASE.mp4"
rm -f "$OUT"

echo "[host] run ffmpeg_encode_file ($DURATION s clip)"
"$BIN" "$OUT_BASE" "$DURATION"

SIZE="$(wc -c < "$OUT" | tr -d ' ')"
[ "$SIZE" -gt 0 ] || { echo "[host] FAIL: example_out.mp4 is empty"; exit 1; }

echo "[host] ffprobe $OUT"
ffprobe -v error -show_entries format=format_name,duration \
        -show_entries stream=codec_name,width,height,r_frame_rate \
        -of default=noprint_wrappers=1 "$OUT"
echo "size=$SIZE bytes"

INFO="$(ffprobe -v error -show_entries stream=codec_name,width,height \
        -of default=noprint_wrappers=1 "$OUT")"
FMT="$(ffprobe -v error -show_entries format=format_name \
        -of default=noprint_wrappers=1 "$OUT")"

echo "$INFO" | grep -qx 'codec_name=h264' || { echo '[host] FAIL: not h264'; exit 1; }
echo "$INFO" | grep -qx 'width=640'       || { echo '[host] FAIL: width != 640'; exit 1; }
echo "$INFO" | grep -qx 'height=480'      || { echo '[host] FAIL: height != 480'; exit 1; }
echo "$FMT"  | grep -q 'mp4'              || { echo "[host] FAIL: not an mp4 container ('$FMT')"; exit 1; }

echo "[host] PASS: valid 640x480 MP4/H.264 ($SIZE bytes)"
