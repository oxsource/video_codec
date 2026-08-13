# Contract: Backend Implementation

**Owner**: `src/framework/backend/*` | **Implements**: `encoder-contract.md` |
**Depends on**: `api`, `core`, `utils`, and exactly one external dependency.

Each backend is an independent `BUILD.bazel` subdirectory. It subclasses
`VideoEncoder`/`AudioEncoder` and satisfies the encoder contract plus the backend-specific
obligations below. Backends **must not** depend on each other.

## Common obligations (all backends)

1. Subclass `VideoEncoder`/`AudioEncoder`; implement `Init`, both `Encode` overloads,
   `Flush`, `Release`.
2. `Init()` opens the external encoder (FFmpeg `AVCodecContext`, NDK `AMediaCodec`, or
   `VTCompressionSession`) and returns `kOk` only when ready to accept frames.
3. Convert between the framework's `VideoFrame`/`AudioFrame` and the backend's native
   frame type using `utils` for pixel/sample conversion.
4. Map external errors to `Status` (never throw).
5. `Release()` frees ALL external resources and is idempotent.
6. Register itself only via the factory `select()` wiring in `public` — no global
   side effects at load.

## FFmpeg backend (`backend/ffmpeg`)

- External dep: `@ffmpeg` (static, force-loaded — see ADR-001/003).
- Video: H.264 → `libx264`, HEVC → `libx265`; Audio: AAC / Opus.
- CPU path: build `AVFrame` (NV12 / YUV420P) honoring `stride`; `Encode(NativeBuffer)`
  returns `kUnsupportedOperation` for the software path (`CreateInputSurface()` →
  `nullptr`).
- Hardware path: `Encode(NativeBuffer)` may forward to FFmpeg HW encoders
  (NVENC/VA-API/V4L2M2M) when `NativeBuffer.backend==kFFmpeg`; otherwise
  `kUnsupportedOperation`.
- Output `AVPacket` → `Packet` (Annex-B via `AVPacketToAnnexB` when needed);
  keyframe via `AV_PKT_FLAG_KEY`.

## Android backend (`backend/android`)

- External dep: `@androidndk//:media` (registered via `android_ndk_repository`, R2).
- Video: `video/avc` / `video/hevc`; Audio: `audio/mp4a-latm` (AAC).
- CPU path: `AMediaCodec_getInputBuffer` copy of NV12.
- Surface path: `AMediaCodec_createInputSurface()` → `ANativeWindow`, wrapped as
  `InputSurface`; `NativeBuffer.handle` points to `AHardwareBuffer*`. `CreateInputSurface()`
  returns a real surface (the only backend that does).
- Output: `AMediaCodec_getOutputBuffer` → `Packet`; SPS/PPS via
  `BUFFER_FLAG_CODEC_CONFIG`; keyframe via `BUFFER_FLAG_KEY_FRAME`.
- Compiled only under the Android toolchain (`target_compatible_with`); never linked on
  host.

## Darwin backend (`backend/darwin`) — RESERVED

- Not implemented this phase. Location exists; `Create` falls back to FFmpeg on Apple
  (ADR-004). Full contract added when the VideoToolbox backend is designed.

## Acceptance

A backend is "contract-complete" when:
- `bazel test` integration tests encode ≥1 frame and the output decodes (FFmpeg via
  `ffprobe`; Android via the decoder or a pulled MP4), AND
- all `encoder-contract.md` guarantees (lifecycle, no-exceptions, thread model) hold.
