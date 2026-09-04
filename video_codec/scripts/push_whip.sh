#!/usr/bin/env bash
# push_whip.sh — WHIP 推流快速验证：用 ffmpeg 合成 testsrc 源推到 mediamtx。
#
# 主要用于对照验证：mediamtx 告警（如 FU-A/非起始分片）是否与上游编码流
# 或本端 RTP 打包有关 —— 这个脚本的 ffmpeg whip muxer 是独立参考实现。
#
# Usage:
#   scripts/push_whip.sh                          # http://localhost:8889/test/whip
#   scripts/push_whip.sh <path>                   # http://localhost:8889/<path>/whip
#   scripts/push_whip.sh test 640x480 300k        # 指定分辨率 / 码率
#   DURATION=5 scripts/push_whip.sh test          # 推 5 秒后自动退出
#   BITRATE=800k GOP=30 scripts/push_whip.sh test # 环境变量覆盖
#
# Environment:
#   DURATION   有限时长（秒），空 = 一直推
#   BITRATE    视频码率（默认 600k）
#   GOP        GOP 大小（默认 60）
#   RATE       帧率（默认 30）
set -euo pipefail

WHIP_BASE="${WHIP_BASE:-http://localhost:8889}"
STREAM_PATH="${1:-test}"
RESOLUTION="${2:-1280x720}"
RATE="${RATE:-30}"
BITRATE="${BITRATE:-600k}"
GOP="${GOP:-60}"

URL="${WHIP_BASE}/${STREAM_PATH}/whip"

cmd=(ffmpeg -hide_banner -loglevel warning -re -f lavfi
  -i "testsrc=size=${RESOLUTION}:rate=${RATE}"
  -c:v libx264 -pix_fmt yuv420p -preset ultrafast -tune zerolatency
  -profile:v baseline -b:v "${BITRATE}" -g "${GOP}")
if [[ -n "${DURATION:-}" ]]; then
  cmd+=(-t "${DURATION}")
fi
cmd+=(-f whip "${URL}")

echo "== ffmpeg WHIP push =="
echo "  url:       ${URL}"
echo "  source:    testsrc ${RESOLUTION} @ ${RATE} fps"
echo "  encoding:  libx264 baseline, ${BITRATE}, gop ${GOP}"
[[ -n "${DURATION:-}" ]] && echo "  duration:  ${DURATION} s"
echo

exec "${cmd[@]}"