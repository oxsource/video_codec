<!-- SPECKIT START -->
For additional context about technologies to be used, project structure,
shell commands, and other important information, read the current plan:
specs/007-input-surface-api/plan.md (Implementation Plan), and the design
doc at codec/doc/project_bootstrap.md. Architecture & engineering design (module
dependencies, lifecycle, backend selection, threading, output queue + consumer (file/stream), muxer layering, error handling, logging, ADRs)
lives under codec/doc/architecture/ and codec/doc/adrs/. The active feature (spec 007)
adds Android MediaCodec input-surface support on top of the spec-006 backend:
`VideoEncoder::CreateInputSurface()` returns `void*` (ANativeWindow* on Android,
nullptr elsewhere) and `api/input_surface.h` is removed; the caller draws into
the returned hardware input surface (`createInputSurface`, zero-copy) and the
system delivers buffers to the encoder; `Flush()` signals EOS via
`signalEndOfInputStream` (drain has a deadline — EOS-after-signalEndOfInputStream
is not guaranteed across Android encoders, so the drain must not block forever).
Surface mode is declared via `VideoConfig.input_surface`
(COLOR_FormatSurface is a configure-time choice) and is mutually exclusive with
CPU-frame input (`Encode(VideoFrame)`). The base spec-006 backend provides
`VideoEncoder` (H.264/HEVC, CPU input), `AudioEncoder` (AAC), and `Muxer`
(AMediaMuxer, via a seekable temp file replayed to the ByteSink at Finish()),
self-registered like the FFmpeg backend; NDK wiring
(`android_ndk_repository` + `//third_party/android_ndk:android_media_codec` →
libmediandk) is in place. Spec decisions (006): CPU path + (007) input surface,
AAC only, MediaMuxer muxer included.
<!-- SPECKIT END -->
