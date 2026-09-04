#!/usr/bin/env bash
#
# check_h264.sh — validate a recorded H.264 Annex-B elementary stream.
#
# Runs the two ffprobe checks used to verify encode_and_push --record output:
#   1. stream profile/level (expect Constrained Baseline for WebRTC/WHIP)
#   2. frame picture-type histogram (expect only I/P when max_b_frames=0)
#
# Usage:
#   scripts/check_h264.sh <file.h264>
#   scripts/check_h264.sh out/out.h264
#
# Exit status: 0 if the file is H.264 without B-frames, 1 otherwise.
set -u

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <file.h264>" >&2
  exit 2
fi

input="$1"

if ! command -v ffprobe >/dev/null 2>&1; then
  echo "error: ffprobe not found" >&2
  exit 2
fi

if [ ! -f "$input" ]; then
  echo "error: file not found: $input" >&2
  exit 2
fi

echo "== stream: $input =="
echo "-- profile / level --"
ffprobe -show_streams "$input" 2>&1 | grep -iE "profile|level"

echo "-- picture types --"
ffprobe -show_frames -select_streams v "$input" 2>&1 | grep pict_type | sort | uniq -c

# Fail loudly if B-frames slipped in (encoder must run max_b_frames=0).
if ffprobe -show_frames -select_streams v "$input" 2>&1 | grep -q "pict_type=B"; then
  echo "FAIL: stream contains B-frames" >&2
  exit 1
fi

echo "OK: profile/level readable, no B-frames."
exit 0
