# Changelog

All notable changes to video_codec are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/) and the project uses SemVer.

## [Unreleased]

### Added
- Public include surface is now workspace-root based (mediapipe-style): codec headers
  are included as `codec/src/framework/...`, stream as `stream/src/...`; the
  `--copt=-Icodec` / `--copt=-Istream` global include-root hacks were removed so external
  consumers of `@video_codec` no longer inherit a workspace-root flag. A consumer only
  needs `deps` on the published library and `#include <video_codec/video_codec.h>`.
- `libvideo_codec_shared` (`.so`/`.dylib`) is the self-contained publish artifact: FFmpeg
  is statically force-loaded inside, so a consumer links only the shared library and never
  has to know about or link FFmpeg. `ByteSink`/`FileByteSink` are exported via
  `VIDEO_CODEC_API` and `alwayslink` so the Muxer output target resolves from the shared lib.

### Known limitations
- **Internal symbols exported from `libvideo_codec_shared`**: on macOS the Bazel toolchain
  (Homebrew LLVM clang) does not honor `-fvisibility=hidden` / `visibility("hidden")` for
  Mach-O symbol export, so backend internals (e.g. `FFmpegMuxer`) appear in the dylib's
  export table. Functionally harmless (a consumer cannot and does not call them); it is
  visible if you inspect the symbol table. Fixing it requires either an Apple-clang
  toolchain or a link-time export allow-list (macOS `-exported_symbols_list`), the latter
  blocked by Bazel 6 not mounting `data` files into the link sandbox.
- **Runtime dependency on `libx264`**: `libvideo_codec_shared` links the encoder's x264
  as a Homebrew-provided dylib (not statically embedded), so a deployment host needs
  `libx264` present at runtime. Consumers of the shared library do not need to know or link
  x264 themselves.
- **No per-consumer FFmpeg force-load (ADR-001 relaxation)**: the `encode_file` example no
  longer hand-writes `data` + `-force_load` FFmpeg link flags; it relies on the backend
  `alwayslink` pulling the FFmpeg archive as a normal static lib. Verified on host for both
  MP4 (muxer + AAC + H.264) and raw Annex-B paths. The FFmpeg backend tests keep their
  explicit `force_load` because they drive broader codec paths (codec registration lists,
  `_ff_prefetch_aarch64`) that a lazily-linked archive may drop.
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
