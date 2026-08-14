<!-- SPECKIT START -->
For additional context about technologies to be used, project structure,
shell commands, and other important information, read the current plan:
specs/006-android-mediacodec-backend/plan.md (Implementation Plan), and the design
doc at codec/doc/project_bootstrap.md. Architecture & engineering design (module
dependencies, lifecycle, backend selection, threading, output queue + consumer (file/stream), muxer layering, error handling, logging, ADRs)
lives under codec/doc/architecture/ and codec/doc/adrs/. The active feature (spec 006)
implements the Android MediaCodec backend in `backend/android`: it provides the
`VideoEncoder` (H.264/HEVC, CPU input), `AudioEncoder` (AAC), and `Muxer`
(AMediaMuxer, via a seekable temp file replayed to the ByteSink at Finish())
implementations, self-registered like the FFmpeg backend; NDK wiring
(`android_ndk_repository` + `//third_party/android_ndk:android_media_codec` →
libmediandk) is an in-scope prerequisite. Spec decisions: CPU path only (no
Surface zero-copy), AAC only, MediaMuxer muxer included.
<!-- SPECKIT END -->
