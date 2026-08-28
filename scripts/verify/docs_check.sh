#!/usr/bin/env bash
# Quick doc consistency check. Invoked by `make docs-check`.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DOC="$ROOT/codec/doc/project_bootstrap.md"
TASKS="$ROOT/specs/001-project-scaffold/tasks.md"
FFMPEG_BZ="$ROOT/codec/third_party/ffmpeg/BUILD.bazel"
fail=0

echo "[docs] 1/4 task completion"
done_count="$(grep -cE '^- \[X\]' "$TASKS")"
open_count="$(grep -cE '^- \[ \]' "$TASKS" || true)"
echo "  tasks done=$done_count open=$open_count"
[ "$open_count" -eq 0 ] || { echo "[docs] FAIL: $open_count open task(s) remain"; fail=1; }

echo "[docs] 2/4 deferred dirs absent"
for d in "$ROOT/codec/examples" "$ROOT/codec/src/framework/backend/darwin"; do
  if [ -e "$d" ]; then echo "[docs] FAIL: deferred $d exists"; fail=1; fi
done
echo "  ok"

echo "[docs] 3/4 bootstrap mentions rules_foreign_cc source build"
grep -q 'rules_foreign_cc' "$DOC" || { echo "[docs] FAIL: project_bootstrap.md omits rules_foreign_cc"; fail=1; }

echo "[docs] 4/4 ffmpeg built via configure_make"
grep -q 'configure_make' "$FFMPEG_BZ" || { echo "[docs] FAIL: ffmpeg BUILD does not use configure_make"; fail=1; }

if [ "$fail" -ne 0 ]; then echo "[docs] FAIL"; exit 1; fi
echo "[docs] PASS: docs consistent with produced layout"
