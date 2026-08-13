# Interface Contracts: video_codec Public API

**Date**: 2026-08-11

These are the public contracts the scaffold must make compilable and that
downstream encoder work will implement. They are derived from
`codec/doc/project_bootstrap.md` §3.4–§3.5. This scaffold phase creates only the
`core` types and the `api` abstract interfaces as compilable stubs; backends are
implemented in Phase 2.

## 1. Core Types Contract (`core/`)

```cpp
namespace video::codec {

enum class VideoCodecType { kH264, kHEVC };
enum class AudioCodecType { kAAC, kOpus };
enum class PixelFormat { kI420, kNV12, kRGBA };
enum class SampleFormat { kS16, kF32, kS16Planar, kF32Planar };
enum class BitrateMode { kConstant, kVariable };
enum class Backend { kAuto, kAndroid, kDarwin, kFFmpeg };

struct VideoFrame { /* format, width, height, timestamp_us, planes[3], stride[3] */ };
struct AudioFrame { /* format, sample_rate, channels, timestamp_us, data */ };
struct Packet { /* type: kVideo/kAudio, data, pts_us, keyframe */ };

struct NativeBuffer {              // zero-copy pointer object
    Backend backend = Backend::kAuto;
    void* handle = nullptr;        // AHardwareBuffer* (Android) / device ptr (FFmpeg HW)
    PixelFormat format = PixelFormat::kNV12;
    int width = 0; int height = 0;
    int64_t timestamp_us = 0;
    int fence_fd = -1;
};

struct VideoEncoderConfig { /* codec, width, height, fps, bitrate, bitrate_mode,
                              gop_size, input_format, force_backend */ };
struct AudioEncoderConfig { /* codec, sample_rate, channels, bitrate, force_backend */ };

}  // namespace video::codec
```

## 2. VideoEncoder Contract (`api/`)

```cpp
class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;
    static std::unique_ptr<VideoEncoder> Create(const VideoEncoderConfig&);
    virtual bool Init() = 0;
    virtual bool Encode(const VideoFrame& frame, Packet* out) = 0;   // CPU path
    virtual bool Encode(const NativeBuffer& buf, Packet* out) = 0;   // zero-copy
    virtual std::unique_ptr<InputSurface> CreateInputSurface() { return nullptr; }
    virtual bool Flush(Packet* out) = 0;
    virtual void Release() = 0;
};

class InputSurface {
public:
    virtual ~InputSurface() = default;
    virtual void* GetNativeSurface() = 0;     // e.g. ANativeWindow*
    virtual bool QueueFrame(int64_t timestamp_us) = 0;
};
```

## 3. AudioEncoder Contract (`api/`)

```cpp
class AudioEncoder {
public:
    virtual ~AudioEncoder() = default;
    static std::unique_ptr<AudioEncoder> Create(const AudioEncoderConfig&);
    virtual bool Init() = 0;
    virtual bool Encode(const AudioFrame& frame, Packet* out) = 0;
    virtual bool Flush(Packet* out) = 0;
    virtual void Release() = 0;
};
```

## 4. Factory Selection Contract

- `Create()` resolves `force_backend` first, then compile-time platform macro.
- `Backend::kAuto` on Android → `backend/android`; elsewhere → `backend/ffmpeg`.
- Apple (`darwin`) currently falls back to `backend/ffmpeg` (VideoToolbox reserved).

## 5. Stability / Compatibility

- All public symbols must be annotated `VIDEO_CODEC_API` and exported only from
  `public/`; internal modules stay hidden (`-fvisibility=hidden`).
- ABI changes to `core` types or `api` interfaces require a major version bump note
  in `project_bootstrap.md`.
