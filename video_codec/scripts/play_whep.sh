#!/usr/bin/env bash
# play_whep.sh — WHEP 拉流快速验证：用 GStreamer 播放 encode_and_push / ffmpeg 推上来的流。
#
# 参数来自本仓库推送端实际配置（对照代码）：
#   signal host/path      stream/src/examples/stream_conf.json  -> http://localhost:8889/test/whip
#   H.264 RTP payload PT  参考使用的 PT 127；本仓库 libdatachannel 端为 96
#   （webrtc_backend.cc:453,457）。两者均可，只要视频/音频 PT 不重复（见下）。
#   音频                  mediamtx WHEP 端要求列出音频轨道：视频-only 流也会
#                         补一条 PCMU（peer_connection.go建议音频 track 占位），
#                         且音频 PT 不能与视频 PT 相同。因此必须给 audio-caps
#                         = PCMU/8000（PT 0），否则报 "codecs not supported by
#                         client"。实测 PT 96+OPUS 音频会与视频 96 冲突而失败。
#   视频参数              640x480 @30fps, 2 Mbps, gop 30, Constrained Baseline
#                         codec/src encode_and_push.cc SetupCodec()
#
# Usage:
#   scripts/play_whep.sh                          # http://localhost:8889/test/whep
#   scripts/play_whep.sh <path>                   # http://localhost:8889/<path>/whep
#
# Environment:
#   WHEP_BASE    WHEP 服务地址（默认 http://localhost:8889）
#   VIDEO_CAPS   视频 RTP caps（默认 H264 PT 127 / 90000）
#   AUDIO_CAPS   音频 RTP caps（默认 PCMU PT 0 / 8000；务必与视频 PT 不同）
#   SINK         视频渲染 sink（默认 autovideosink；终端验证可用 fakesink）
#
# 注意：whepsrc 已废弃（GStreamer 1.28 推荐 whepclientsrc+signaller），
#   但 whepclientsrc 的属性模型不同，此脚本保持与参考命令一致的 whepsrc。
set -euo pipefail

WHEP_BASE="${WHEP_BASE:-http://localhost:8889}"
STREAM_PATH="${1:-test}"
VIDEO_CAPS="${VIDEO_CAPS:-application/x-rtp,media=video,encoding-name=H264,payload=127,clock-rate=90000}"
AUDIO_CAPS="${AUDIO_CAPS:-application/x-rtp,media=audio,encoding-name=PCMU,payload=0,clock-rate=8000}"
SINK="${SINK:-autovideosink}"

ENDPOINT="${WHEP_BASE}/${STREAM_PATH}/whep"

cmd=(gst-launch-1.0 whepsrc whep-endpoint="${ENDPOINT}" use-link-headers=true
  video-caps="${VIDEO_CAPS}")
if [[ -n "${AUDIO_CAPS}" ]]; then
  cmd+=(audio-caps="${AUDIO_CAPS}")
fi
cmd+=(! rtph264depay ! decodebin ! "${SINK}")

echo "== GStreamer WHEP play =="
echo "  endpoint:  ${ENDPOINT}"
echo "  video:     ${VIDEO_CAPS}"
[[ -n "${AUDIO_CAPS}" ]] && echo "  audio:     ${AUDIO_CAPS}"
echo "(Crtl-C to stop)"
echo

exec "${cmd[@]}"