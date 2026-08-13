# Contract: Encoder Interface (`VideoEncoder` / `AudioEncoder`)

**Owner**: `src/framework/api` | **Implemented by**: each `backend/*` |
**Frozen by**: `public-api.md`, `data-model.md` §5.

This contract defines what every backend must satisfy. It is the single source of truth
for the abstract interface; `backend-contract.md` adds backend-specific obligations.

## VideoEncoder

```cpp
class VideoEncoder {
  public:
    virtual ~VideoEncoder() = default;

    // Resolve backend + allocate, but do NOT open the external encoder yet.
    static std::unique_ptr<VideoEncoder> Create(const VideoEncoderConfig& config);

    virtual StatusCode Init() = 0;                 // → Initialized

    // CPU path. enc->Init() must have succeeded. Returns kNotInitialized otherwise.
    virtual Result<Packet> Encode(const VideoFrame& frame) = 0;

    // Zero-copy path. backend must understand NativeBuffer.backend; returns
    // kUnsupportedOperation if the backend cannot consume the handle type.
    virtual Result<Packet> Encode(const NativeBuffer& buf) = 0;

    // Returns a drawable surface, or nullptr if the backend has no Surface support
    // (FFmpeg software path returns nullptr).
    virtual std::unique_ptr<InputSurface> CreateInputSurface() { return nullptr; }

    virtual Result<Packet> Flush() = 0;     // → Flushed (drain + emit final pkt)
    virtual void Release() = 0;                     // free external resources → Released
};
```

## AudioEncoder

```cpp
class AudioEncoder {
  public:
    virtual ~AudioEncoder() = default;
    static std::unique_ptr<AudioEncoder> Create(const AudioEncoderConfig& config);
    virtual StatusCode Init() = 0;
    virtual Result<Packet> Encode(const AudioFrame& frame) = 0;
    virtual Result<Packet> Flush() = 0;
    virtual void Release() = 0;
    // No Surface/InputSurface for audio.
};
```

## Behavioral guarantees

1. **Lifecycle**: only the transitions in `data-model.md` §5 are valid. Invalid calls
   return the documented `StatusCode` and leave state unchanged. `Release()` is
   idempotent.
2. **No exceptions**: every method returns `StatusCode` / `Result<T>`; internal failures
   are converted to `StatusCode` before returning.
3. **Thread safety**: a single `Encoder` instance is **not** thread-safe. The caller
   serializes calls or owns one instance per thread (see `threading.md`).
4. **Output**: video `Packet::data` is **Annex-B** (SPS/PPS emitted at the first
   keyframe / on `Flush`); `keyframe` marks IDR frames. Audio `Packet::data` is a
   complete access unit (e.g. ADTS AAC).
5. **Key frames**: `Init` may request a key frame; backends emit SPS/PPS at the first
   output and on config change.
6. **Resource ownership**: `Encode` does not take ownership of `frame`/`buf`; the caller
   keeps them alive until the call returns. Returned `Packet` is owned by the
   caller.

## Error mapping (minimum)

| Condition | StatusCode |
|-----------|------------|
| Called before `Init` | `kNotInitialized` |
| `Encode(NativeBuffer)` on unsupported handle | `kUnsupportedOperation` |
| External encoder error | `kEncodeFailed` |
| Bad config (w=0/h=0/unknown codec) | `kInvalidArgument` |
| Platform has no matching backend | `kPlatformUnsupported` |
