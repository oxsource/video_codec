# Changelog

All notable changes to video_codec are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/) and the project uses SemVer.

## [Unreleased]

### Added
- Architecture & engineering design (spec `002-architecture-engineering-design`):
  module dependency graph, encoder lifecycle, backend-selection model, threading,
  error-handling (`Status` / `Result<T>`), `LogSlot` logging interface, build/test/CI
  conventions, and ADRs (static force-loaded FFmpeg, `select()`-per-platform, BSD
  `libtool` merge, deferred VideoToolbox).
- Encoder framework implementation (spec `002-...` task list): abstract `VideoEncoder` /
  `AudioEncoder` interfaces, lifecycle state machine, encoder factory with self-registration,
  FFmpeg video/audio backends, bounded SPSC `PacketQueue`, `PacketSource::Await` +
  `FileSinkConsumer` transport, and their unit tests.
- Core utilities & public surface (spec `003-core-utils-public-api`): `utils` module
  (YUV420P↔NV12 conversion, stride helpers, PCM sample-format conversion) and the single
  public umbrella header `video_codec/video_codec.h` re-exporting the frozen public contracts
  via the existing `VIDEO_CODEC_API` export macro.
- Encoder-to-queue push wiring (spec `004-encoder-queue-wiring`): optional `SetOutputSink`
  on `VideoEncoder`/`AudioEncoder` enables push mode — every produced packet is handed to
  the output queue (single destination) while the pull API stays the default. Includes
  end-to-end tests with the real FFmpeg encoder (order, zero-loss under `kBlock`,
  back-pressure pacing, flush + caller-owned end-of-stream). Also fixes pre-existing
  FFmpeg backend bugs exposed by these first runtime tests: force-loaded archive linking
  (ADR-001), `av_bsf` `par_in` allocation, I420 chroma `CopyFrame` width, and
  `Err<T>` template deduction.
- MP4 muxing & runnable example: FFmpeg now also builds `libavformat` (mp4/mov muxer),
  and a new `mux` package provides `Mp4MuxConsumer` — a `PacketConsumer` that converts
  Annex-B packets to AVCC samples, builds avcC extradata from the first keyframe, and
  writes a standard MP4 (verified: 60 frames @ 30fps, correct duration, fully decodable).
  The `ffmpeg_encode_file` example outputs `.mp4` (or raw `.h264` by extension) and is
  wired into `make host_ffmpeg_example` / `make verify`. Also fixes the encoder flush bug
  that dropped x264's buffered frames (`if (drain_eof) break;` removed from Drain).

## [0.1.0] - 2026-08-11

### Added (scaffold, spec `001-project-scaffold`)
- Bazel 6.5.0 workspace: platforms, third-party wrappers, module stubs, public umbrella.
- `ffmpeg_spike`: validates FFmpeg `libx264` encodes a valid 320×240 H.264 stream.
- `mediacodec_spike`: Android `AMediaCodec` encode spike (cross-build, NDK-gated).
- FFmpeg 6.1 built from source via `rules_foreign_cc` (static, force-loaded).
- `make`-based categorized validation mechanism (`codec/mk/` + `scripts/verify/`).
