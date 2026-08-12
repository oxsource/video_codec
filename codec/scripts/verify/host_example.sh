#!/usr/bin/env bash
# Host example: build + run the encode_to_file demo (SMPTE color bars -> H.264)
# and assert the output is a valid 640x480 H.264 stream with ffprobe.
# Invoked by `make host-example` (and as part of `make host-verify`).
#
# Args: [seconds] — clip duration (default 2, keeps CI fast; the demo default is 5).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

DURATION="${1:-2}"

echo "[host] bazel build //src/examples:encode_to_file"
bazel build //src/examples:encode_to_file

BIN="$(find -L bazel-bin -path '*/examples/encode_to_file' -type f | head -1)"
[ -n "$BIN" ] || { echo "[host] FAIL: encode_to_file binary not found"; exit 1; }

OUT="$ROOT/bazel-bin/example_out.h264"
rm -f "$OUT"

echo "[host] run encode_to_file ($DURATION s clip)"
"$BIN" "$OUT" "$DURATION"

SIZE="$(wc -c < "$OUT" | tr -d ' ')"
[ "$SIZE" -gt 0 ] || { echo "[host] FAIL: example_out.h264 is empty"; exit 1; }

INFO="$(ffprobe -v error -show_entries stream=codec_name,width,height \
        -of default=noprint_wrappers=1 "$OUT")"
echo "$INFO"
echo "size=$SIZE bytes"

echo "$INFO" | grep -qx 'codec_name=h264' || { echo '[host] FAIL: not h264'; exit 1; }
echo "$INFO" | grep -qx 'width=640'       || { echo '[host] FAIL: width != 640'; exit 1; }
echo "$INFO" | grep -qx 'height=480'      || { echo '[host] FAIL: height != 480'; exit 1; }

echo "[host] PASS: valid 640x480 H.264 ($SIZE bytes)"
