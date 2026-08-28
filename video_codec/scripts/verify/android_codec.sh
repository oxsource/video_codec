#!/usr/bin/env bash
# Android cross-build + on-device verification of the encode_file example
# (MediaCodec backend).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

MODE="${1:-run}"
CLIP_SECONDS="${2:-2}"

echo "[android] build //codec/src/examples:encode_file (--config android_arm64)"
bazel build //codec/src/examples:encode_file --config android_arm64

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
    adb -s "${DEVICE}" shell "cd /data/local/tmp && ./encode_file --raw clip ${CLIP_SECONDS}"
    adb -s "${DEVICE}" pull /data/local/tmp/clip-android.h264 "$ROOT/out/clip-android.h264" >/dev/null

    OUT="$ROOT/out/clip-android.h264"
    SIZE="$(wc -c < "$OUT" | tr -d ' ')"
    [ "$SIZE" -gt 0 ] || { echo "[android] FAIL: clip-android.h264 is empty"; exit 1; }

    echo "[android] pulled $OUT ($SIZE bytes)"
    DECODE_ERR="$(ffmpeg -v error -f h264 -i "$OUT" -f null - 2>&1 >/dev/null)"
    if [[ -n "${DECODE_ERR}" ]]; then
        echo "[android] FAIL: h264 decode errors:"
        echo "${DECODE_ERR}"
        exit 1
    fi
    echo "[android] PASS: valid raw H.264 stream, no muxer ($SIZE bytes)"
    exit 0
fi

RUN_FLAG=""
BASE="clip-android"
if [[ "${MODE}" == "surface" ]]; then
  RUN_FLAG="--surface"
  BASE="clip-android-surface"
fi
adb -s "${DEVICE}" shell "cd /data/local/tmp && ./encode_file ${RUN_FLAG} clip ${CLIP_SECONDS}"
adb -s "${DEVICE}" pull "/data/local/tmp/${BASE}.mp4" "$ROOT/out/${BASE}.mp4" >/dev/null

OUT="$ROOT/out/${BASE}.mp4"
SIZE="$(wc -c < "$OUT" | tr -d ' ')"
[ "$SIZE" -gt 0 ] || { echo "[android] FAIL: ${BASE}.mp4 is empty"; exit 1; }

INFO="$(ffprobe -v error -show_entries stream=codec_name,codec_type \
        -of default=noprint_wrappers=1 "$OUT")"
FMT="$(ffprobe -v error -show_entries format=format_name \
        -of default=noprint_wrappers=1 "$OUT")"
echo "[android] pulled $OUT ($SIZE bytes)"
echo "$INFO"

echo "$INFO" | grep -q 'codec_name=h264' || { echo '[android] FAIL: no h264 video stream'; exit 1; }
echo "$INFO" | grep -q 'codec_name=aac'  || { echo '[android] FAIL: no aac audio stream'; exit 1; }
echo "$FMT"  | grep -q 'mp4'             || { echo "[android] FAIL: not an mp4 container ('$FMT')"; exit 1; }

DECODE_ERR="$(ffmpeg -v error -i "$OUT" -f null - 2>&1 >/dev/null)"
if [[ -n "${DECODE_ERR}" ]]; then
    echo "[android] FAIL: decode errors:"
    echo "${DECODE_ERR}"
    exit 1
fi

echo "[android] PASS: valid H.264 + AAC MP4 ($SIZE bytes)"
