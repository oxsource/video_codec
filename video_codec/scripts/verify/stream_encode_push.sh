#!/usr/bin/env bash
# Host validation of the stream encode_and_push example (WebRTC/WHIP push).
# Invoked by `make host-encode-push`.
#
# Builds and runs //stream/src/examples:encode_and_push for RUN_SECONDS (default
# 10). Recording is off by default (matching --record-only pushes). The example
# needs a WHIP endpoint (stream_conf.json
# signal.host, default http://localhost:8889); when unreachable the stream falls
# back to "recording only" but the encoder pipeline still runs — so the pass
# criterion is the number of frames actually encoded, not remote reachability.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

RUN_SECONDS="${1:-10}"

echo "[stream] bazel build //stream/src/examples:encode_and_push"
bazel build //stream/src/examples:encode_and_push

BIN="$(find -L bazel-bin -path '*/examples/encode_and_push' -type f | head -1)"
[ -n "$BIN" ] || { echo "[stream] FAIL: encode_and_push binary not found"; exit 1; }

mkdir -p "$ROOT/out"
echo "[stream] run encode_and_push (${RUN_SECONDS}s)"
# The example logs the encoded-frame summary to stderr (VC_LOG): "done — N frames".
OUTPUT="$("$BIN" --config "$ROOT/stream/src/examples/stream_conf.json" \
       --seconds "$RUN_SECONDS" 2>&1)" || {
         echo "$OUTPUT"
         echo "[stream] FAIL: encode_and_push exited nonzero"
         exit 1
       }
echo "$OUTPUT"
FRAMES="$(printf '%s\n' "$OUTPUT" | grep -oE 'done — [0-9]+ frames' | grep -oE '[0-9]+' | tail -1 || true)"
EXPECTED="$((RUN_SECONDS * 30))"

if ! [[ "$FRAMES" =~ ^[0-9]+$ ]]; then
  echo "[stream] FAIL: no encoded-frame summary found (got '$FRAMES')"
  exit 1
fi

echo "[stream] encoded $FRAMES frames (expected ~$EXPECTED for ${RUN_SECONDS}s @30fps)"
if [ "$FRAMES" -lt "$((EXPECTED * 9 / 10))" ]; then
  echo "[stream] WARN: encoded $FRAMES < 90% of expected $EXPECTED — check WHIP/encoder"
fi

echo "[stream] PASS: encode_and_push encoded $FRAMES frames over ${RUN_SECONDS}s"
