# Data Model: Architecture & Engineering Design (Phase 1)

Entity/type model for the video_codec framework, derived from `project_bootstrap.md`
and frozen by the contracts in `contracts/`. This is the design-time model (no code
yet); it defines the shapes the implementation phase must honor.

## 1. Module Model

| Module | Responsibility | Depends on | Visibility |
|--------|---------------|-----------|------------|
| `core` | Frame/packet/config types, `NativeBuffer`, enums, `StatusCode`, `LogSlot` | — | `__subpackages__` + `tests` |
| `api` | `VideoEncoder`/`AudioEncoder` abstract, `InputSurface`, factory | `core` | `__subpackages__` + `tests` |
| `utils` | Pixel/sample format conversion, stride | `core` | `__subpackages__` + `tests` |
| `backend/android` | MediaCodec video+audio | `core`,`api`,`utils`,`@androidndk` | `__subpackages__` + `tests` |
| `backend/ffmpeg` | FFmpeg video+audio | `core`,`api`,`utils`,`@ffmpeg` | `__subpackages__` + `tests` |
| `backend/darwin` | VideoToolbox (reserved) | `core`,`api`,`utils`,`VideoToolbox.framework` | `__subpackages__` + `tests` |
| `public` | Umbrella header, export macro, `select()` backend link | all above | `//visibility:public` |

**Invariant**: no backends depend on each other; `core` depends on nothing; only
`public` is externally visible.

## 2. Value Types (core)

```cpp
enum class VideoCodecType { kH264, kHEVC };
enum class AudioCodecType { kAAC, kOpus };
enum class PixelFormat { kI420, kNV12, kRGBA };
enum class SampleFormat { kS16, kF32, kS16Planar, kF32Planar };
enum class BitrateMode { kConstant, kVariable };
enum class Backend { kAuto, kAndroid, kDarwin, kFFmpeg };

struct VideoFrame {
    PixelFormat format = PixelFormat::kNV12;
    int width = 0; int height = 0; int64_t timestamp_us = 0;
    std::vector<uint8_t> planes[3];   // I420: Y,U,V; NV12: Y,UV
    int stride[3] = {0,0,0};
};

struct AudioFrame {
    SampleFormat format = SampleFormat::kS16;
    int sample_rate = 48000; int channels = 2; int64_t timestamp_us = 0;
    std::vector<uint8_t> data;        // interleaved PCM
};

struct EncodedPacket {
    std::vector<uint8_t> data;        // Annex-B preferred for video
    int64_t pts_us = 0; bool keyframe = false;
};

struct AudioPacket {
    std::vector<uint8_t> data;        // e.g. ADTS AAC
    int64_t pts_us = 0; bool keyframe = false;  // always false for audio
};

struct NativeBuffer {                 // zero-copy pointer object
    Backend backend = Backend::kAuto;
    void* handle = nullptr;           // AHardwareBuffer* (Android) / device ptr (FFmpeg HW)
    PixelFormat format = PixelFormat::kNV12;
    int width = 0; int height = 0; int64_t timestamp_us = 0;
    int fence_fd = -1;
};
```

### Validation rules
- `VideoFrame`: `width>0 && height>0`; `format==kRGBA ⇒ planes[2] empty`; `stride[i]>=width` for used planes.
- `AudioFrame`: `sample_rate>0 && channels>0`; `data` size consistent with `SampleFormat` × frames.
- `EncodedPacket`/`AudioPacket`: `!data.empty()`; `keyframe` meaningful only for video.
- `NativeBuffer`: `handle!=nullptr` when `backend!=kAuto`.

## 3. Config Types (api)

```cpp
struct VideoEncoderConfig {
    VideoCodecType codec = VideoCodecType::kH264;
    int width = 0; int height = 0; int fps = 30; int bitrate = 4'000'000;
    BitrateMode bitrate_mode = BitrateMode::kConstant;
    int gop_size = 0;                       // 0 = auto
    PixelFormat input_format = PixelFormat::kNV12;
    Backend force_backend = Backend::kAuto; // kAuto → platform select
};

struct AudioEncoderConfig {
    AudioCodecType codec = AudioCodecType::kAAC;
    int sample_rate = 48000; int channels = 2; int bitrate = 128'000;
    Backend force_backend = Backend::kAuto;
};
```

## 4. Error Model (core)

```cpp
enum class StatusCode {
    kOk = 0,
    kInvalidArgument, kNotInitialized, kEncodeFailed,
    kUnsupportedFormat, kBackendUnavailable, kPlatformUnsupported,
    kUnsupportedOperation,
};

template <typename T> class Result {  // holds StatusCode | T
  public:
    static Result Ok(T v);
    static Result Error(StatusCode c);
    bool ok() const; StatusCode status() const; const T& value() const;
};
```

All fallible public APIs return `StatusCode` or `Result<T>`; **no exceptions cross the
public API** (see `research.md` R7, `codec/doc/architecture/error-handling.md`).

## 5. Encoder Lifecycle State Machine

States: `Created → Initialized → Encoding → Flushed → Released`.

| From \ Event | Init() | Encode() | Flush() | Release() |
|--------------|--------|----------|---------|-----------|
| Created | →Initialized | err kNotInitialized | err | →Released |
| Initialized | err (already) | →Encoding | →Flushed | →Released |
| Encoding | err | →Encoding | →Flushed | →Released |
| Flushed | →Initialized (reuse) | err kNotInitialized | →Flushed | →Released |
| Released | err | err | err | err |

Transitions not listed return `StatusCode` (no state mutation, no crash). Diagram in
`codec/doc/architecture/lifecycle-model.md`.

## 6. Backend Selection Model

`ResolveBackend(force_backend)`:
- `kAndroid` (and platform is android) → `MediaCodecVideoEncoder`
- `kDarwin` → `FFmpegVideoEncoder` (VideoToolbox reserved; Apple falls back)
- `kFFmpeg` / `kAuto` (non-android) → `FFmpegVideoEncoder`

Bazel `select()` links **only** the resolved backend + its external dep. Diagram in
`codec/doc/architecture/backend-selection.md`.

## 7. Ownership Model

- Owned objects (encoders, frames' `planes`/`data`): `std::unique_ptr` / value types.
- Non-owning references (config, callback targets): raw pointers / references.
- Zero-copy handles: `NativeBuffer` is a **pointer object** — the framework never takes
  ownership of `handle`; the caller keeps it alive for the encode call's duration.
