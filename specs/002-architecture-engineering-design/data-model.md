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

enum class PacketType { kVideo, kAudio };   // unified packet tag

struct Packet {
    PacketType type = PacketType::kVideo;
    std::vector<uint8_t> data;        // video: Annex-B preferred; audio: e.g. ADTS AAC
    int64_t pts_us = 0; bool keyframe = false;  // audio: always false
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
- `Packet`: `!data.empty()`; `keyframe` meaningful only for video.
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

## 8. Output Transport Model (ring buffer)

Encoded packets are handed off to consumers through a bounded **SPSC ring buffer**
(`PacketQueue`). This decouples the encoder (producer) from the consumer (muxer /
network sender / file writer).

### Interfaces

```cpp
class OutputSink {                       // producer endpoint (encoder writes here)
  public:
    virtual ~OutputSink() = default;
    // Unified packet; PacketType routes it to the video/audio ring.
    virtual StatusCode Submit(Packet&& pkt) = 0;
    virtual StatusCode Flush() { return StatusCode::kOk; }
};

class PacketSource {              // consumer endpoint (pops here)
  public:
    virtual ~PacketSource() = default;
    // Non-blocking: returns false if empty. Blocking variant takes a deadline.
    virtual PopResult Pop(Packet& out, int64_t deadline_us) = 0;
};
```

`PacketQueue` implements **both** interfaces: producer side exposes `OutputSink`,
consumer side exposes `PacketSource`.

### Entity: PacketQueue

| Field | Type | Meaning |
|-------|------|---------|
| `capacity` | `size_t` (power of two) | ring slots (fixed at construction) |
| `slots[]` | `Packet[]` | pre-allocated, packets **moved** in/out |
| `head_` / `tail_` | `std::atomic<size_t>` | consumer / producer indices (mod mask) |
| `policy` | `Backpressure` | back-pressure when full; default `kBlock` |

### Relationship to encoder

- The encoder, after a successful `Encode()`, forwards the packet to a configured
  `OutputSink` (see `output-queue-contract.md`). The pull API (`Encode()` returning
  `Result<Packet>`) is unchanged; pushing is an **optional** sink mode.
- Ownership transfers to the queue via move; the producer no longer references the packet
  after `Submit()` returns.

### Validation rules

- `capacity > 0` and a power of two (fast index masking).
- `Submit` on a full queue honors `policy`: `kBlock` waits, `kDropOldest` overwrites the
  oldest unconsumed slot, `kError` returns `kBackendUnavailable` (or a dedicated
  back-pressure code).
- `TryPop` on an empty queue returns `false` (non-blocking); a blocking `Pop(deadline)`
  variant is provided for consumers that must wait.

### Consumer side (`PacketConsumer`)

The consumer is decoupled from the encoder by a `PacketConsumer` interface; a
`PacketPump` drain loop on the consumer thread bridges `PacketSource` →
`PacketConsumer`.

```cpp
class PacketConsumer {
  public:
    virtual ~PacketConsumer() = default;
    // One Consume for both media; PacketType tells the consumer which media.
    virtual StatusCode Consume(Packet&& pkt) = 0;
    virtual StatusCode Flush() { return StatusCode::kOk; }
    virtual StatusCode Finish() { return StatusCode::kOk; } // EOS / teardown
};
```

Two target consumers implement this (both deferred to implementation, designed now):

| Consumer | Role | Key obligations |
|----------|------|-----------------|
| `FileSinkConsumer` | Save `.h264`/`.aac` or mux to `.mp4`/`.mkv` | preserve order + keyframe/SPS-PPS; flush on `Finish()` |
| `StreamConsumer` | 推流: RTMP / SRT / WebRTC | frame Annex-B → protocol units; connection lifecycle + reconnect; pace by `pts_us`; propagate back-pressure (slow socket → slow `Consume` → ring fills → encoder slows) |

`PacketPump::Run(src, consumer)` loops `Pop` → `Consume`; calls `Finish()` at EOS. A
blocking `Pop(deadline)` avoids busy-spin.

### Multiple consumers (record + stream)

One ring buffer = one reader (SPSC). For simultaneous file + stream, instantiate **two**
`PacketQueue`s off the encoder, or insert a **tee** fanning one source to N sinks.
The queue + `PacketConsumer` contract stay SPSC-clean either way.

