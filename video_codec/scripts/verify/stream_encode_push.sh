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

echo "[stream] run encode_and_push (${RUN_SECONDS}s)"
LOG="$ROOT/out/encode_and_push.log"
mkdir -p "$ROOT/out"
"$BIN" --config "$ROOT/stream/src/examples/stream_conf.json" \
       --seconds "$RUN_SECONDS" >"$LOG" 2>&1

# The example logs the encoded-frame summary to stderr (VC_LOG): "done — N frames".
FRAMES="$(grep -oE 'done — [0-9]+ frames' "$LOG" | grep -oE '[0-9]+' | tail -1 || true)"
EXPECTED="$((RUN_SECONDS * 30))"

if [ -z "$FRAMES" ] || [ "$FRAMES" -eq 0 ]; then
  echo "[stream] FAIL: no frames encoded (see $LOG)"
  tail -20 "$LOG"
  exit 1
fi

echo "[stream] encoded $FRAMES frames (expected ~$EXPECTED for ${RUN_SECONDS}s @30fps)"
if [ "$FRAMES" -lt "$((EXPECTED * 9 / 10))" ]; then
  echo "[stream] WARN: encoded $FRAMES < 90% of expected $EXPECTED — check WHIP/encoder"
fi

if grep -q 'stream Start failed' "$LOG"; then
  echo "[stream] NOTE: WHIP endpoint unreachable — ran in encoder-only fallback"
fi

echo "[stream] PASS: encode_and_push encoded $FRAMES frames over ${RUN_SECONDS}s"
