# Contract: Zero-Copy Input (`NativeBuffer` + `InputSurface`)

**Owner**: `api` (interface) + `backend/*` (semantics) | **Frozen by**: `data-model.md` §2,
`encoder-contract.md`.

Defines the unified zero-copy input abstraction so Android `MediaCodec` Surface and
FFmpeg hardware encoders share one interface (`Encode(const NativeBuffer&)` and
`CreateInputSurface()`).

## NativeBuffer (pointer object)

```cpp
struct NativeBuffer {
    Backend backend = Backend::kAuto;  // which backend understands `handle`
    void*   handle  = nullptr;         // AHardwareBuffer* (Android) / device ptr (FFmpeg HW)
    PixelFormat format = PixelFormat::kNV12;
    int width = 0; int height = 0; int64_t timestamp_us = 0;
    int fence_fd = -1;                 // optional release/submit fence
};
```

### Obligations
1. **The framework never takes ownership of `handle`.** The caller must keep the
   underlying buffer alive for the entire duration of the `Encode(NativeBuffer)` call
   and honor any `fence_fd` semantics.
2. `backend` selects who can consume the buffer. A backend receiving a `NativeBuffer`
   whose `backend` it does not implement returns `kUnsupportedOperation`.
3. `handle == nullptr` with `backend != kAuto` is invalid → `kInvalidArgument`.
4. Android: `handle` is an `AHardwareBuffer*`; the backend imports it (e.g.
   `AHardwareBuffer_lock`) for the encode.
5. FFmpeg HW: `handle` is the device-memory pointer of an NVENC/VA-API/V4L2M2M surface;
   the software FFmpeg path returns `kUnsupportedOperation`.

## InputSurface (drawable surface)

```cpp
class InputSurface {
  public:
    virtual ~InputSurface() = default;
    virtual void* GetNativeSurface() = 0;             // e.g. ANativeWindow*
    virtual bool QueueFrame(int64_t timestamp_us) = 0;
};
```

### Obligations
1. Only the **Android** backend returns a non-null `InputSurface` from
   `CreateInputSurface()`. The FFmpeg (software) backend returns `nullptr`.
2. `GetNativeSurface()` returns the platform surface object the caller draws into
   (Android `ANativeWindow*`). Other backends: returns `nullptr`.
3. `QueueFrame(ts)` notifies the encoder a frame is available at `ts`; safe to call from
   the caller's render thread. The backend serializes against its own encode thread.

## CPU vs zero-copy paths (summary)

| Path | VideoEncoder API | Android | FFmpeg (sw) | FFmpeg (hw) |
|------|-----------------|---------|-------------|-------------|
| CPU frame | `Encode(VideoFrame)` | ✅ copy NV12 | ✅ AVFrame | ✅ AVFrame |
| Surface | `CreateInputSurface()` | ✅ real | ❌ nullptr | ❌ nullptr |
| NativeBuffer | `Encode(NativeBuffer)` | ✅ AHardwareBuffer* | ❌ UnsupportedOp | ✅ device ptr |

This contract lets business code stay backend-agnostic: it either feeds
`VideoFrame`/`AudioFrame`, or draws to an `InputSurface` / passes a `NativeBuffer`,
and the chosen backend adapts.
