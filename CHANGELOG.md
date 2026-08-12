# Changelog

All notable changes to video_codec are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/) and the project uses SemVer.

## [Unreleased]

### Added
- Architecture & engineering design (spec `002-architecture-engineering-design`):
  module dependency graph, encoder lifecycle, backend-selection model, threading,
  error-handling (`StatusCode` / `Result<T>`), `LogSlot` logging interface, build/test/CI
  conventions, and ADRs (static force-loaded FFmpeg, `select()`-per-platform, BSD
  `libtool` merge, deferred VideoToolbox).
- Encoder framework implementation (spec `002-...` task list): abstract `VideoEncoder` /
  `AudioEncoder` interfaces, lifecycle state machine, encoder factory with self-registration,
  FFmpeg video/audio backends, bounded SPSC `EncodedPacketQueue`, `PacketPump` +
  `FileSinkConsumer` transport, and their unit tests.
- Core utilities & public surface (spec `003-core-utils-public-api`): `utils` module
  (YUV420P↔NV12 conversion, stride helpers, PCM sample-format conversion) and the single
  public umbrella header `video_codec/video_codec.h` re-exporting the frozen public contracts
  via the existing `VIDEO_CODEC_API` export macro.

## [0.1.0] - 2026-08-11

### Added (scaffold, spec `001-project-scaffold`)
- Bazel 6.5.0 workspace: platforms, third-party wrappers, module stubs, public umbrella.
- `ffmpeg_spike`: validates FFmpeg `libx264` encodes a valid 320×240 H.264 stream.
- `mediacodec_spike`: Android `AMediaCodec` encode spike (cross-build, NDK-gated).
- FFmpeg 6.1 built from source via `rules_foreign_cc` (static, force-loaded).
- `make`-based categorized validation mechanism (`codec/mk/` + `scripts/verify/`).
