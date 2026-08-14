#!/usr/bin/env bash
# Android cross-build + on-device verification of the encode_file example
# (MediaCodec backend), in isolation steps (research R6/R8):
#
#   build    — cross-compile only (CI gate)
#   raw      — video-only: MediaCodec video encoder -> raw Annex-B .h264
#              (no muxer). Pulls the stream and decode-checks it on the host.
#   run      — full A/V mux: MediaCodec video+audio + MediaMuxer -> MP4.
#              Pulls the MP4 and ffprobe + decode-checks it on the host.
#
# The Android side is MediaCodec-only (no FFmpeg, spec 006 R5); the host FFmpeg
# baseline is covered by `make host_ffmpeg_codec`. Requires ANDROID_NDK_HOME for
# the cross-build and a connected device/emulator for the raw/run modes.
#
# Usage:
#   android_codec.sh build           # cross-compile only (CI gate)
#   android_codec.sh raw [seconds]   # video-only, no muxer
#   android_codec.sh run [seconds]   # full A/V MP4
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

MODE="${1:-run}"
CLIP_SECONDS="${2:-2}"

echo "[android] build //src/examples:encode_file (--config android_arm64)"
bazel build //src/examples:encode_file --config android_arm64

if [[ "${MODE}" == "build" ]]; then
    echo "[android] build OK"
    exit 0
fi

BIN="$(find -L bazel-bin -path '*/examples/encode_file' -type f | head -1)"
[ -n "${BIN}" ] || { echo "[android] FAIL: encode_file binary not found"; exit 1; }

DEVICE="$(adb devices | awk 'NR>1 && $2=="device" {print $1; exit}')"
[ -n "${DEVICE}" ] || {
    echo "[android] FAIL: no adb device/emulator connected (start one, then retry)"
    exit 1
}

mkdir -p "$ROOT/out"
echo "[android] run encode_file on ${DEVICE} (${CLIP_SECONDS}s clip, mode=${MODE})"
adb -s "${DEVICE}" push "${BIN}" /data/local/tmp/encode_file >/dev/null
adb -s "${DEVICE}" shell "chmod +x /data/local/tmp/encode_file"

if [[ "${MODE}" == "raw" ]]; then
    # Step 2: video-only, no muxer. FileConsumer writes a raw Annex-B H.264
    # elementary stream; this isolates the MediaCodec video encoder.
    adb -s "${DEVICE}" shell "cd /data/local/tmp && ./encode_file --raw clip ${CLIP_SECONDS}"
    adb -s "${DEVICE}" pull /data/local/tmp/clip-android.h264 "$ROOT/out/clip-android.h264" >/dev/null

    OUT="$ROOT/out/clip-android.h264"
    SIZE="$(wc -c < "$OUT" | tr -d ' ')"
    [ "$SIZE" -gt 0 ] || { echo "[android] FAIL: clip-android.h264 is empty"; exit 1; }

    echo "[android] pulled $OUT ($SIZE bytes)"
    # Decode the Annex-B elementary stream; any decoder error means the
    # encoder output is malformed.
    DECODE_ERR="$(ffmpeg -v error -f h264 -i "$OUT" -f null - 2>&1 >/dev/null)"
    if [[ -n "${DECODE_ERR}" ]]; then
        echo "[android] FAIL: h264 decode errors:"
        echo "${DECODE_ERR}"
        exit 1
    fi
    echo "[android] PASS: valid raw H.264 stream, no muxer ($SIZE bytes)"
    exit 0
fi

# Mode == run: full A/V mux (MediaMuxer). Run from /data/local/tmp so the
# -<backend> output (clip-android.mp4) lands there.
adb -s "${DEVICE}" shell "cd /data/local/tmp && ./encode_file clip ${CLIP_SECONDS}"
adb -s "${DEVICE}" pull /data/local/tmp/clip-android.mp4 "$ROOT/out/clip-android.mp4" >/dev/null

OUT="$ROOT/out/clip-android.mp4"
SIZE="$(wc -c < "$OUT" | tr -d ' ')"
[ "$SIZE" -gt 0 ] || { echo "[android] FAIL: clip-android.mp4 is empty"; exit 1; }

INFO="$(ffprobe -v error -show_entries stream=codec_name,codec_type \
        -of default=noprint_wrappers=1 "$OUT")"
FMT="$(ffprobe -v error -show_entries format=format_name \
        -of default=noprint_wrappers=1 "$OUT")"
echo "[android] pulled $OUT ($SIZE bytes)"
echo "$INFO"

echo "$INFO" | grep -q 'codec_name=h264' || { echo '[android] FAIL: no h264 video stream'; exit 1; }
echo "$INFO" | grep -q 'codec_name=aac'  || { echo '[android] FAIL: no aac audio stream'; exit 1; }
echo "$FMT"  | grep -q 'mp4'             || { echo "[android] FAIL: not an mp4 container ('$FMT')"; exit 1; }

# Decode pass: stream checks cannot catch malformed sample framing (e.g.
# libstagefright writing an empty first NAL when the sample starts with a start
# code — see research R7) — decode the whole file and fail on any decoder error.
DECODE_ERR="$(ffmpeg -v error -i "$OUT" -f null - 2>&1 >/dev/null)"
if [[ -n "${DECODE_ERR}" ]]; then
    echo "[android] FAIL: decode errors:"
    echo "${DECODE_ERR}"
    exit 1
fi

echo "[android] PASS: valid H.264 + AAC MP4 ($SIZE bytes)"
