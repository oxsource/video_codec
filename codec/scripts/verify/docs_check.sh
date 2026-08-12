#!/usr/bin/env bash
# Quick doc consistency check (Phase 5 T026). Invoked by `make docs-check`.
# Verifies the produced scaffold matches what the docs claim, without a build.
#
# The bazel workspace is codec/ but specs/ lives at the repo root (beside
# .specify/), so resolve the repo root by walking up to the .specify marker.
set -euo pipefail

_repo_root() {
  local d="$(cd "$(dirname "$0")" && pwd)"
  while [ "$d" != "/" ]; do
    [ -d "$d/.specify" ] && { echo "$d"; return 0; }
    d="$(dirname "$d")"
  done
  echo "$(cd "$(dirname "$0")/../../.." && pwd)"
}

ROOT="$(_repo_root)"
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
