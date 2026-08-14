# Video Codec - Project Bootstrap

> Version: 0.2
>
> Status: Draft
>
> Purpose: Initial project definition for AI-assisted specification-driven development.

---

# 1. Vision (Why)

## 1.1 Project Vision

Video Codec 是一套**跨平台音视频编码框架（Cross-Platform Audio/Video Encoding Framework）**。

音视频编码在不同平台上的实现差异巨大：Android 拥有高效的 `MediaCodec` 硬件编码器（视频 H.264/HEVC、音频 AAC），Apple 平台提供 `VideoToolbox`，而其他平台（Linux / Windows / 服务器）通常依赖 FFmpeg（libx264 / libx265 / AAC / Opus）。这些 API 在接口形态、参数语义、异步模型，以及"输入数据如何送达编码器"上都各不相同——有的要求拷贝 CPU 内存，有的支持直接绘制到 `Surface`/NativeWindow 做零拷贝。

Video Codec 的目标是在这些原生编码能力之上，构建一**套统一的、声明式配置的编码抽象层**，对业务侧暴露一致的接口，底层按平台自动选择最优后端实现，并统一处理两类输入：

- **CPU 数据交换**：以内存中的 `VideoFrame` / `AudioFrame` 喂入（所有后端通用）
- **Surface / 原生句柄零拷贝**：以 `InputSurface` 或 `NativeBuffer` 指针对象直接绘制 / 传递（Android `MediaCodec` 原生支持；FFmpeg 硬件路径通过同一指针对象接入）

从而让调用方以相同方式完成"喂入帧 → 产出编码码流"的全过程，视频与音频同理。

### Design Philosophy

- **Write Once, Encode Anywhere**：上层业务只依赖统一 `VideoEncoder` / `AudioEncoder` 接口，不感知底层后端
- **Platform-Best-Backend**：每个平台自动选用该平台最高效的编码实现（优先硬件编码）
- **Two Input Models, One Interface**：同时支持 CPU 内存帧与 Surface/原生句柄零拷贝输入，FFmpeg 与 MediaCodec 共用 `NativeBuffer` 指针对象
- **Zero-Copy Friendly**：输入采用平台原生像素格式与 Surface（如 Android 的 `Surface` / `AHardwareBuffer`），最大限度避免 CPU 回读与格式转换
- **Layered & Modular**：`core`（类型） / `api`（抽象与工厂） / `backend/*`（平台实现，各自独立子目录） / `utils`（格式转换）各层独立
- **Bazel-First**：作为独立 Bazel Library 对外提供，支持 `select()` 按平台切换后端依赖

### Primary Backends (Phase 1)

一期**重点实现两套后端**，覆盖绝大多数落地场景：

1. **Android — MediaCodec**（NDK `AMediaCodec`）：硬件编码，支持输入 Surface 零拷贝，覆盖视频与音频（AAC）
2. **通用 — FFmpeg**（`libavcodec`）：覆盖 Linux / Windows / 服务器等非 Android 平台，软件或硬件（NVENC / VA-API / V4L2M2M）编码，覆盖视频与音频（AAC / Opus）

Apple `VideoToolbox`（macOS / iOS）**不在一期实现范围，后续（Phase 2+）支持**，仅在架构中预留 `backend/darwin/` 位置，当前在 Apple 平台上 `Create` 回退到 FFmpeg 后端。

---

## 1.2 Goals

- 提供统一的 `VideoEncoder` 抽象接口，覆盖 H.264 / HEVC 编码
- 提供统一的 `AudioEncoder` 抽象接口，覆盖 AAC / Opus 编码
- 提供 `VideoEncoder::Create` / `AudioEncoder::Create` 工厂，按平台自动选择后端
- **实现 Android `MediaCodec` 后端**（NDK `AMediaCodec`，硬件编码，视频 + 音频）
- **实现 FFmpeg 后端**（`libx264` / `libx265` / AAC / Opus），覆盖非 Android 平台
- 支持**两种输入模型**：CPU 内存帧（`VideoFrame` / `AudioFrame`）与零拷贝 Surface / 原生句柄（`InputSurface` / `NativeBuffer`）
- 提供原始帧类型 `VideoFrame` / `AudioFrame` 与编码包类型 `VideoPacket` / `AudioPacket`
- 提供像素格式与采样格式转换工具（`utils`）
- 作为独立基础库，供其他 Bazel 项目依赖使用
- 保持模块化设计，便于新增后端（如 Apple `VideoToolbox`、Windows `MediaFoundation`）

---

## 1.3 Non-Goals (Phase 1)

- **Apple `VideoToolbox` 后端**：架构预留 `backend/darwin/`，一期不实现，**后续（Phase 2+）支持**
- 视频**解码**（仅做编码；解码可在后续以同构方式扩展）
- 更多容器格式（MKV / TS / WebM muxing，后续；v1 经 api `Muxer` 接口支持 MP4，由 FFmpeg 后端实现）
- 网络推流 / 传输（RTMP / SRT / WebRTC，后续）
- 滤镜 / 前处理（缩放、裁剪、颜色空间转换流水线，后续）
- 音视频合流复用为单一容器（一期视频、音频各自独立产出码流）
- 嵌入式 / RTOS 平台支持（后续）

---

# 2. Requirements (What)

## 2.1 Functional Requirements

### FR-001 Unified Video & Audio Encoder Interface

业务侧通过统一接口完成编码，不感知后端类型：

```cpp
// Video
auto enc = VideoEncoder::Create(VideoConfig{
    .codec = VideoCodecType::kH264,
    .width = 1920, .height = 1080, .fps = 30,
    .bitrate = 4'000'000,
});
enc->Init();
while (has_frame) {
    Packet pkt;
    if (enc->Encode(frame, &pkt)) { /* consume pkt.data */ }
}
enc->Flush();
enc->Release();

// Audio (parallel API shape)
auto aenc = AudioEncoder::Create(AudioConfig{
    .codec = AudioCodecType::kAAC,
    .sample_rate = 48000, .channels = 2, .bitrate = 128'000,
});
```

### FR-002 Platform Backend Selection

`*Encoder::Create` 内部按编译期平台宏自动选择后端：

- `android` → `MediaCodecVideoEncoder` / `MediaCodecAudioEncoder`
- 其他（含 `darwin` 当前）→ `FFmpegVideoEncoder` / `FFmpegAudioEncoder`

一期**只实现上述两套后端**。Apple `VideoToolbox` 后端在架构中预留（Phase 2+），当前 Apple 平台回退到 FFmpeg 后端。允许通过 `backend` 显式覆盖（调试 / 测试用途）。

### FR-003 Android MediaCodec Backend (Primary)

- 使用 NDK `AMediaCodec` 进行编码
- 视频：H.264 / HEVC；音频：AAC
- 视频输入支持两条路径：
  - **CPU 路径**：`AMediaCodec_getInputBuffer` 喂入 NV12
  - **Surface 零拷贝路径**：`AMediaCodec_createInputSurface` 拿到 `ANativeWindow`，业务直接绘制，编码器消费
- 输出通过 `AMediaCodec_getOutputBuffer` 取得码流（Annex-B），以 `BUFFER_FLAG_CODEC_CONFIG` 处理 SPS/PPS，以 `BUFFER_FLAG_KEY_FRAME` 标记关键帧

### FR-004 FFmpeg Backend (Primary)

- 使用 FFmpeg `libavcodec`：视频 H.264 → `libx264`、HEVC → `libx265`；音频 AAC / Opus
- 视频输入支持两条路径：
  - **CPU 路径**：构造 YUV420P / NV12 `AVFrame`
  - **硬件句柄路径**：通过 `NativeBuffer` 指针对接 FFmpeg 硬件编码器（NVENC / VA-API / V4L2M2M），与 MediaCodec 共用同一 `NativeBuffer` 接口
- 覆盖所有非 Android 平台（Linux / Windows），作为默认后备实现

### FR-005 Surface & NativeBuffer Zero-Copy Input

框架必须提供与平台无关的零拷贝输入抽象，使 MediaCodec 的 Surface 模型与 FFmpeg 硬件路径共享同一接口：

- `InputSurface`：可绘制的输入 Surface 对象。Android 返回真实 `ANativeWindow` 封装；FFmpeg 软件路径返回 `nullptr`（不支持）
- `NativeBuffer`：描述原生缓冲区句柄的**指针对象**（`void* handle` + 后端标识 + 格式/尺寸/时间戳）。Android 侧指向 `AHardwareBuffer*`；FFmpeg 硬件侧指向对应器件内存指针。编码统一通过 `Encode(const NativeBuffer&)` 接收

### FR-006 Video / Audio Frame & Packet Types

- `VideoFrame`：原始视频帧，含像素格式、宽高、平面数据/stride、时间戳（微秒）
- `AudioFrame`：原始音频帧（PCM），含采样格式、采样率、声道数、数据、时间戳
- `VideoPacket` / `AudioPacket`：编码包，含码流字节、PTS、是否为关键帧（音频恒为 false）

### FR-007 Pixel & Sample Format Conversion Utils

- 视频：YUV420P（三平面）↔ NV12（半平面）、stride 对齐、必要时 RGB → YUV
- 音频：交错 / 平面 PCM 互转、采样格式（s16 / f32）转换

### FR-008 Public Library

Video Codec 应作为 Bazel Library 对外提供：

```python
deps = ["@video_codec//src/framework/public:video_codec"]
```

### FR-009 Example

项目必须提供完整示例，验证两条路径：
- 视频：构造配置 → 喂入若干 CPU 帧（或绘制到 `InputSurface`）→ 产出 .h264/.hevc
- 音频：构造配置 → 喂入 PCM → 产出 .aac/.opus

---

## 2.2 Non-Functional Requirements

- **轻量**：核心库体积可控，Android 侧不强制引入 FFmpeg（仅平台后端按需链接）
- **模块化**：core / api / backend / utils 各层独立，接口清晰；**每个 backend 为独立子目录**
- **易扩展**：新增后端只需实现 `VideoEncoder` / `AudioEncoder` 并向工厂注册
- **易测试**：core 类型与 utils 有单元测试；后端可通过 mock 帧验证流程
- **跨平台**：架构层面预留 Android / Apple / 通用后端，通过 `select()` 切换依赖
- **零拷贝优先**：提供 Surface / `NativeBuffer` 原生输入路径，避免无谓 CPU 拷贝

---

# 3. Design (How)

## 3.1 Overall Architecture

```
Application Code (configure *EncoderConfig, feed VideoFrame/AudioFrame)
    │
    ▼
VideoEncoder / AudioEncoder API (abstract)
    │                         └─ EncoderFactory (platform select)
    │
    ├── CPU path : Encode(VideoFrame / AudioFrame)        ← 所有后端通用
    └── Zero-copy: Encode(NativeBuffer) / InputSurface    ← MediaCodec Surface, FFmpeg HW
    │
    ├── Android  : MediaCodecVideoEncoder / MediaCodecAudioEncoder  (AMediaCodec, HW)
    ├── Apple    : VideoToolboxVideoEncoder (Phase 2+, reserved)
    └── Generic  : FFmpegVideoEncoder / FFmpegAudioEncoder (libx264/5, AAC, Opus)
    │
    ▼
Packet (kVideo / kAudio) → caller (mux / transmit / store)
```

## 3.2 Core Modules

### Module Overview

```
codec/src/framework/
│
├── core/        # 基础类型：VideoFrame, AudioFrame, VideoPacket, AudioPacket,
│               #           VideoConfig, AudioConfig, MuxerConfig,
│               #           NativeBuffer, enums, PacketSink
├── api/         # 抽象接口：VideoEncoder, AudioEncoder, Muxer, InputSurface, 工厂
├── backend/     # 平台后端实现（每个后端独立子目录，自带 BUILD.bazel）
│   ├── android/ # MediaCodecVideoEncoder / MediaCodecAudioEncoder (NDK AMediaCodec)
│   ├── darwin/  # VideoToolboxVideoEncoder (Phase 2+, reserved)
│   └── ffmpeg/  # FFmpegVideoEncoder / FFmpegAudioEncoder (libx264/5, AAC, Opus)
│                # + FFmpegMuxer（MP4 封装，libavformat）
├── queue/       # PacketQueue（双 ring：视频/音频），PacketSource
├── io/          # ByteSink 输出契约 + FileByteSink/StreamByteSink/TeeByteSink
├── consumer/    # PacketConsumer, FileConsumer（裸流 Annex-B / ADTS）
├── convert/     # libyuv 像素格式转换（PixelConverter）
├── utils/       # 布局/测试图案工具：Stride, SmpteBars, MediaFormat
└── public/      # 公开 API 汇总入口
```

> **关键约定**：每个后端（android / darwin / ffmpeg）都是 `backend/` 下的**独立子目录**，拥有各自的 `BUILD.bazel` 与依赖闭包，互不直接依赖。业务层只依赖 `api/`，通过 `select()` 决定链接哪个后端子目录。

### Module Responsibility

| Module | Responsibility | External Deps |
|--------|---------------|---------------|
| `core` | 帧/包/配置类型、枚举、`NativeBuffer`、错误码 | — |
| `api` | `VideoEncoder`/`AudioEncoder` 抽象、`InputSurface`、工厂与选择 | core |
| `backend/android` | MediaCodec 视频+音频编码实现 | core, api, NDK (media/NdkMediaCodec) |
| `backend/darwin` | VideoToolbox 视频编码实现（Phase 2+, reserved） | core, api, VideoToolbox.framework |
| `backend/ffmpeg` | FFmpeg 视频+音频编码实现 | core, api, FFmpeg (libavcodec/libavutil) |
| `utils` | 像素/采样格式转换、stride 处理 | core |
| `public` | Umbrella header、export macro、汇总 target | 所有模块 |

### Naming Scope

C++ namespace `video::codec`，其下按模块分二层 namespace：

```cpp
namespace video {
namespace codec {

// Core types
struct VideoFrame;
struct AudioFrame;
struct VideoPacket;
struct AudioPacket;
struct VideoConfig;
struct AudioConfig;
struct NativeBuffer;

// API
class VideoEncoder;
class AudioEncoder;
class InputSurface;

}  // namespace codec
}  // namespace video
```

## 3.3 Directory Structure

```
codec/                              # Bazel workspace root
│
├── doc/
│   └── project_bootstrap.md       # This file
│
├── platforms/
│   ├── BUILD                      # config_setting + platform 定义
│   └── platforms.bzl              # config_setting_and_platform 宏
│
├── third_party/
│   ├── ffmpeg/
│   │   └── BUILD.bazel            # FFmpeg cc_library wrapper
│   └── android_ndk/
│       └── BUILD.bazel            # NDK media codec headers / libmedia wrappers
│
├── src/
│   └── framework/
│       ├── core/
│       │   ├── BUILD.bazel
│       │   ├── video_frame.h / video_frame.cc
│       │   ├── audio_frame.h / audio_frame.cc
│       │   ├── encoded_packet.h / encoded_packet.cc
│       │   ├── audio_packet.h / audio_packet.cc
│       │   ├── video_encoder_config.h / video_encoder_config.cc
│       │   ├── audio_encoder_config.h / audio_encoder_config.cc
│       │   ├── native_buffer.h          # NativeBuffer 指针对象定义
│       │   └── enums.h                 # VideoCodecType, AudioCodecType, PixelFormat, SampleFormat, BitrateMode, Backend
│       ├── api/
│       │   ├── BUILD.bazel
│       │   ├── video_encoder.h / video_encoder.cc     # 抽象基类 + Create()
│       │   ├── audio_encoder.h / audio_encoder.cc     # 抽象基类 + Create()
│       │   ├── input_surface.h / input_surface.cc     # InputSurface 抽象
│       │   └── codec_factory.h / codec_factory.cc # 平台选择
│       ├── backend/
│       │   ├── android/                # 独立子目录：MediaCodec 后端
│       │   │   ├── BUILD.bazel
│       │   │   ├── mediacodec_video_encoder.h / .cc
│       │   │   ├── mediacodec_audio_encoder.h / .cc
│       │   │   ├── mediacodec_input_surface.h / .cc   # ANativeWindow 封装
│       │   │   └── mediacodec_utils.h / .cc
│       │   ├── darwin/                 # 独立子目录：VideoToolbox 后端 (Phase 2+, reserved)
│       │   │   ├── BUILD.bazel
│       │   │   └── videotoolbox_video_encoder.h / .cc
│       │   └── ffmpeg/                 # 独立子目录：FFmpeg 后端
│       │       ├── BUILD.bazel
│       │       ├── ffmpeg_video_encoder.h / .cc
│       │       ├── ffmpeg_audio_encoder.h / .cc
│       │       └── ffmpeg_utils.h / .cc
│       ├── utils/
│       │   ├── BUILD.bazel
│       │   ├── pixel_format.h / pixel_format.cc   # YUV420P↔NV12, stride
│       │   ├── color_convert.h / color_convert.cc  # RGB→YUV
│       │   └── sample_format.h / sample_format.cc  # PCM 交错/平面, s16/f32
│       └── public/
│           ├── BUILD.bazel
│           └── include/
│               └── video_codec/
│                   ├── video_codec.h          # Umbrella header
│                   ├── video_codec_export.h   # VIDEO_CODEC_API macro
│                   ├── core.h
│                   ├── api.h
│                   └── utils.h
│
├── examples/
│   ├── BUILD.bazel
│   ├── encode_video_demo.cc        # MVP: CPU 帧 / InputSurface → .h264
│   └── encode_audio_demo.cc        # MVP: PCM → .aac
│
├── tests/
│   ├── BUILD.bazel
│   ├── core_test.cc
│   ├── utils_test.cc               # 格式转换 round-trip
│   └── encoder_test.cc             # 后端 smoke test (mock frames)
│
├── BUILD.bazel                     # Root alias
├── WORKSPACE                       # workspace(name = "video_codec")
├── video_codec_deps.bzl            # External dependency bootstrap
├── .bazelversion                   # 6.5.0
├── .bazelrc
├── .bazelignore
└── .gitignore
```

> **Scope note (scaffold phase vs. design):** The tree above is the *target* layout.
> In the current scaffold phase, `examples/` and `src/framework/backend/darwin/`
> (VideoToolbox) are **intentionally NOT produced** — both are deferred to the
> Phase 2 implementation. The scaffold ships `backend/android` / `backend/ffmpeg`
> stubs plus the two spikes (`src/spike/`), and builds FFmpeg from source via
> `rules_foreign_cc`. `backend/darwin` exists only as a reserved location (the
> `Create` factory currently falls back to FFmpeg on Apple platforms).

## 3.4 Core Type Model

### Enums

```cpp
enum class VideoCodecType { kH264, kHEVC };
enum class AudioCodecType { kAAC, kOpus };
enum class PixelFormat { kI420, kNV12, kRGBA };  // I420 = YUV420P
enum class SampleFormat { kS16, kF32, kS16Planar, kF32Planar };
enum class BitrateMode { kConstant, kVariable };
enum class Backend { kAuto, kAndroid, kFFmpeg };
```

### VideoFrame (CPU path)

```cpp
struct VideoFrame {
    PixelFormat format = PixelFormat::kNV12;
    int width = 0;
    int height = 0;
    int64_t timestamp_us = 0;        // presentation timestamp

    // Semi-planar (NV12): planes[0]=Y, planes[1]=UV.
    // Planar (I420): planes[0]=Y, planes[1]=U, planes[2]=V.
    std::vector<uint8_t> planes[3];
    int stride[3] = {0, 0, 0};       // row bytes per plane (may != width)
};
```

### AudioFrame (CPU path)

```cpp
struct AudioFrame {
    SampleFormat format = SampleFormat::kS16;
    int sample_rate = 48000;
    int channels = 2;
    int64_t timestamp_us = 0;
    std::vector<uint8_t> data;       // interleaved PCM
};
```

### VideoPacket / AudioPacket

音视频为**独立类型**，编译期即可区分，避免 API 混用：

```cpp
struct VideoPacket {
    std::vector<uint8_t> data;       // raw video bitstream (Annex-B preferred)
    int64_t pts_us = 0;
    bool keyframe = false;           // IDR
};

struct AudioPacket {
    std::vector<uint8_t> data;       // raw audio bitstream (e.g. ADTS AAC)
    int64_t pts_us = 0;
    bool keyframe = false;           // always false for audio
};
```

### NativeBuffer (Zero-copy pointer object)

统一描述原生缓冲区句柄，使 MediaCodec 与 FFmpeg 硬件路径共用同一接口：

```cpp
struct NativeBuffer {
    Backend backend = Backend::kAuto; // which backend understands this handle
    void* handle = nullptr;           // e.g. AHardwareBuffer* (Android),
                                      //      or device memory ptr (FFmpeg HW)
    PixelFormat format = PixelFormat::kNV12;
    int width = 0;
    int height = 0;
    int64_t timestamp_us = 0;
    int fence_fd = -1;                // optional release/submit fence
};
```

### VideoConfig / AudioConfig

```cpp
struct VideoConfig {
    VideoCodecType codec = VideoCodecType::kH264;
    int width = 0;
    int height = 0;
    int fps = 30;
    int bitrate = 4'000'000;                 // bits per second
    BitrateMode bitrate_mode = BitrateMode::kConstant;
    int gop_size = 0;                        // 0 = auto (e.g. fps*2)
    PixelFormat input_format = PixelFormat::kNV12;
    Backend backend = Backend::kAuto;  // kAuto → platform select
};

struct AudioConfig {
    AudioCodecType codec = AudioCodecType::kAAC;
    int sample_rate = 48000;
    int channels = 2;
    int bitrate = 128'000;                   // bits per second
    Backend backend = Backend::kAuto;
};
```

## 3.5 Encoder API

### VideoEncoder (CPU + Surface/NativeBuffer)

```cpp
class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;

    // Factory: picks backend by platform (or backend).
    static std::unique_ptr<VideoEncoder> Create(const VideoConfig& config);

    virtual bool Init() = 0;

    // CPU path: encode a memory-backed frame.
    virtual bool Encode(const VideoFrame& frame, Packet* out) = 0;

    // Zero-copy path: encode a native buffer handle (unified pointer object).
    // MediaCodec consumes AHardwareBuffer*; FFmpeg HW consumes device ptr.
    virtual bool Encode(const NativeBuffer& buf, Packet* out) = 0;

    // Create a direct-draw input surface (Android real; FFmpeg returns nullptr).
    virtual std::unique_ptr<InputSurface> CreateInputSurface() { return nullptr; }

    virtual bool Flush(Packet* out) = 0;
    virtual void Release() = 0;
};
```

### InputSurface (abstract)

```cpp
class InputSurface {
public:
    virtual ~InputSurface() = default;

    // Platform native surface object (e.g. Android ANativeWindow*).
    virtual void* GetNativeSurface() = 0;

    // Signal a frame is available for encoding at the given timestamp.
    virtual bool QueueFrame(int64_t timestamp_us) = 0;
};
```

### AudioEncoder (CPU path; no Surface for audio)

```cpp
class AudioEncoder {
public:
    virtual ~AudioEncoder() = default;

    static std::unique_ptr<AudioEncoder> Create(const AudioConfig& config);

    virtual bool Init() = 0;
    virtual bool Encode(const AudioFrame& frame, Packet* out) = 0;
    virtual bool Flush(Packet* out) = 0;
    virtual void Release() = 0;
};
```

### Factory & Backend Selection

```cpp
std::unique_ptr<VideoEncoder> VideoEncoder::Create(const VideoConfig& c) {
    switch (ResolveBackend(c.backend)) {
#if defined(__ANDROID__)
        case Backend::kAndroid:  return std::make_unique<MediaCodecVideoEncoder>(c);
        case Backend::kFFmpeg:   return std::make_unique<FFmpegVideoEncoder>(c);
#endif
        // Apple: Phase 2+ (VideoToolbox); for now fall back to FFmpeg.
        case Backend::kAuto:
        default:                 return std::make_unique<FFmpegVideoEncoder>(c);
    }
}
```

Bazel 侧通过 `select()` 仅链接目标平台所需后端子目录，避免非 Android 包强行携带 NDK、非桌面包强行携带 FFmpeg：

```python
deps = select({
    "//platforms:android_arm64": ["//src/framework/backend/android"],
    "//platforms:darwin_arm64":  ["//src/framework/backend/darwin"],
    "//conditions:default":      ["//src/framework/backend/ffmpeg"],
}) + [
    "//src/framework/core",
    "//src/framework/api",
    "//src/framework/utils",
]
```

## 3.6 Backend Notes

### Android — MediaCodec (Primary, Phase 1)

- 基于 NDK `AMediaCodec`（`<media/NdkMediaCodec.h>`），视频创建 `video/avc` / `video/hevc`，音频创建 `audio/mp4a-latm`（AAC）
- **视频 CPU 路径**：`AMediaCodec_getInputBuffer` 拷贝 NV12
- **视频 Surface 零拷贝路径**：`AMediaCodec_createInputSurface` 取得 `ANativeWindow`，封装为 `InputSurface`；业务直接绘制，`QueueFrame(ts)` 通知编码器取帧。`NativeBuffer` 在此指向 `AHardwareBuffer*`
- **音频路径**：`AudioFrame`（PCM）→ `AMediaCodec` 输入，`BUFFER_FLAG_CODEC_CONFIG` 处理 AudioSpecificConfig
- 输出：`AMediaCodec_getOutputBuffer` → `Packet`（kVideo / kAudio）；关键帧以 `BUFFER_FLAG_KEY_FRAME` 标记

### Generic — FFmpeg (Primary, Phase 1)

- 基于 FFmpeg `libavcodec`：视频 H.264 → `libx264`、HEVC → `libx265`；音频 AAC / Opus
- **视频 CPU 路径**：构造 `AVFrame`（NV12 / YUV420P），按 stride 设置 `linesize`
- **视频硬件句柄路径**：通过 `Encode(const NativeBuffer&)` 接入 FFmpeg 硬件编码器（NVENC / VA-API / V4L2M2M）。与 MediaCodec 共用 `NativeBuffer` 指针对象——FFmpeg 软件路径不支持 Surface，故 `CreateInputSurface()` 返回 `nullptr`
- **音频路径**：`AudioFrame` → `AVFrame`，经 AAC/Opus `AVCodec` 编码
- 输出：`AVPacket` → `Packet`（kVideo / kAudio）；关键帧以 `AV_PKT_FLAG_KEY` 标记

### Apple — VideoToolbox (Reserved, Phase 2+)

- 基于 `VideoToolbox.framework`（`VTCompressionSession`）完成硬件编码
- 一期**不实现**，仅在 `backend/darwin/` 预留位置与 `Create` 回退逻辑（当前 Apple 平台回退到 FFmpeg 后端）

---

# 4. Build System (Bazel 6.5)

## 4.1 Version Requirement

```
6.5.0
```

## 4.2 .bazelversion

```
6.5.0
```

## 4.3 .bazelrc

参考 `native_ui` / `graph_runtime` 的 `.bazelrc` 设计：

```text
build --cxxopt=-std=c++17
build --host_cxxopt=-std=c++17
build --features=visibility=hidden

# Platform aliases (select explicitly for cross-compiles)
build:android_arm64 --platforms=//platforms:android_arm64_platform
build:linux_x86_64 --platforms=//platforms:linux_x86_64_platform
build:darwin_arm64 --platforms=//platforms:darwin_arm64_platform

# No forced default --platforms: Bazel uses the host platform automatically, so
# a macOS dev builds for darwin_arm64 and Linux CI builds for linux_x86_64.
# Cross-compiles (e.g. Android) pass --config android_arm64 explicitly.

test --test_output=errors
```

## 4.4 WORKSPACE

```python
workspace(name = "video_codec")

load("//:video_codec_deps.bzl", "video_codec_setup")
load("@bazel_tools//tools/cpp:cc_configure.bzl", "cc_configure")

video_codec_setup()

# Register the host C/C++ toolchain so cc_library / foreign_cc targets build
# on the dev host (no C++ toolchain is available by default on macOS).
cc_configure()

# FFmpeg is built from source via rules_foreign_cc (configure_make). We rely on
# the preinstalled make / pkg-config on the dev host, so we skip building them
# from source.
load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies(
    register_built_tools = False,
    register_built_pkgconfig_toolchain = False,
)
```

## 4.5 Root BUILD.bazel

```python
package(default_visibility = ["//visibility:public"])

alias(
    name = "video_codec",
    actual = "//src/framework/public:video_codec",
)
```

## 4.6 video_codec_deps.bzl

统一管理所有第三方依赖（FFmpeg、rules_foreign_cc、Android NDK、googletest、bazel_skylib）：

```python
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _ffmpeg():
    # FFmpeg 6.1 release. It is BUILT FROM SOURCE (not globbed) — see
    # third_party/ffmpeg/BUILD.bazel which uses rules_foreign_cc configure_make.
    # sha256 pinned to the official ffmpeg-6.1.tar.xz (verified 2026-08-11).
    http_archive(
        name = "ffmpeg",
        urls = ["https://ffmpeg.org/releases/ffmpeg-6.1.tar.xz"],
        sha256 = "488c76e57dd9b3bee901f71d5c95eaf1db4a5a31fe46a28654e837144207c270",
        strip_prefix = "ffmpeg-6.1",
    )

def _googletest():
    http_archive(
        name = "com_google_googletest",
        sha256 = "8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7",
        strip_prefix = "googletest-1.14.0",
        urls = ["https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"],
    )

def _bazel_skylib():
    http_archive(
        name = "bazel_skylib",
        urls = ["https://github.com/bazelbuild/bazel-skylib/archive/refs/tags/1.6.1.tar.gz"],
        sha256 = "aede1b60709ac12b3461ee0bb3fa097b58a86fbfdb88ef7e9f90424a69043167",
        strip_prefix = "bazel-skylib-1.6.1",
    )

def _rules_foreign_cc():
    # Builds FFmpeg (and future native deps) from source via configure_make.
    http_archive(
        name = "rules_foreign_cc",
        sha256 = "5816f4198184a1e0e682d7e6b817331219929401e2f18358fac7f7b172737976",
        strip_prefix = "rules_foreign_cc-0.10.0",
        url = "https://github.com/bazelbuild/rules_foreign_cc/archive/refs/tags/0.10.0.tar.gz",
    )

def video_codec_setup():
    if not native.existing_rule("bazel_skylib"):
        _bazel_skylib()
    if not native.existing_rule("ffmpeg"):
        _ffmpeg()
    if not native.existing_rule("com_google_googletest"):
        _googletest()
    if not native.existing_rule("rules_foreign_cc"):
        _rules_foreign_cc()
```

> **Note**: FFmpeg is **compiled from source** by `third_party/ffmpeg/BUILD.bazel` (`rules_foreign_cc` `configure_make`); only the release tarball is fetched here. Android NDK 依赖通过 Bazel `android_ndk_repository` 或 `@androidndk` 引入，不在 `http_archive` 中处理；具体接入方式在 `/speckit.plan` 阶段细化。

## 4.7 Platforms

### platforms/BUILD

```python
load("//platforms:platforms.bzl", "config_setting_and_platform")

config_setting_and_platform(
    name = "android_arm64",
    constraint_values = [
        "@platforms//os:android",
        "@platforms//cpu:aarch64",
    ],
)

config_setting_and_platform(
    name = "linux_x86_64",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
)

config_setting_and_platform(
    name = "darwin_arm64",
    constraint_values = [
        "@platforms//os:macos",
        "@platforms//cpu:aarch64",
    ],
)
```

### platforms/platforms.bzl

```python
def config_setting_and_platform(name, constraint_values, parents = None):
    native.config_setting(
        name = name + "_setting",
        constraint_values = constraint_values,
    )
    native.platform(
        name = name,
        constraint_values = constraint_values,
        parents = parents,
    )

def video_codec_select(select_map):
    return select(select_map)
```

## 4.8 Third-Party BUILD Files

### third_party/ffmpeg/BUILD.bazel

FFmpeg 6.1 is **compiled from source** via `rules_foreign_cc` `configure_make`, not
globbed into a `cc_library` (a glob can't supply the `configure`-generated `config.h`
and won't cross-compile). The build produces a single BSD-format static archive
`libffmpeg.a` (libavcodec + libavutil merged) plus the install include dir, then
exposes it through a `genrule` → `cc_import` → `alwayslink` `cc_library` so consumers
can force-load the whole archive.

```python
load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")

package(default_visibility = ["//visibility:public"])

# Full FFmpeg source tree, globbed so configure_make can copy it into its sandbox.
filegroup(name = "ffmpeg_source", srcs = glob(["**/*"]))

configure_make(
    name = "ffmpeg_codec_impl",
    lib_source = "@ffmpeg//:ffmpeg_source",
    configure_command = "configure",
    configure_options = [
        "--disable-avdevice", "--disable-avfilter", "--disable-avformat",
        "--disable-postproc", "--disable-swresample", "--disable-swscale",
        "--enable-gpl", "--enable-libx264", "--enable-encoder=libx264",
        "--enable-decoder=h264", "--enable-parser=h264",
        "--disable-programs", "--enable-pic",
        "--enable-neon",   # keep aarch64 helper asm (defines _ff_prefetch_aarch64)
        "--extra-cflags='-I/opt/homebrew/include -Wno-implicit-function-declaration'",
        "--extra-ldflags=-L/opt/homebrew/lib",
    ],
    env = {"PKG_CONFIG_PATH": "/opt/homebrew/lib/pkgconfig"},
    targets = ["install"],
    out_include_dir = "include",
    out_static_libs = ["libffmpeg.a"],
    # Merge into one BSD-format archive without extracting (ar -x would clobber
    # the duplicate-basename videodsp.o members and drop the aarch64 definition).
    postfix_script = """
set -e
cd "$$INSTALLDIR$$/lib"
/usr/bin/libtool -static -o libffmpeg.a libavcodec.a libavutil.a
/usr/bin/ranlib libffmpeg.a
rm -f libavcodec.a libavutil.a
""",
    linkopts = ["-L/opt/homebrew/lib", "-lx264"],  # x264 from dev host
)

# configure_make emits two outputs (static lib + include dir); collapse to one .a
# so $(execpath)/cc_import resolve a single file.
genrule(
    name = "ffmpeg_codec_archive",
    srcs = [":ffmpeg_codec_impl"],
    outs = ["libffmpeg.a"],
    cmd = "for f in $(locations :ffmpeg_codec_impl); do case \"$$f\" in *.a) cp \"$$f\" $@;; esac; done",
)

cc_import(name = "ffmpeg_codec_import", static_library = ":ffmpeg_codec_archive")

cc_library(name = "ffmpeg_codec", deps = [":ffmpeg_codec_import"], alwayslink = True)
```

> **Why static + BSD-merged + force-load (not shared, not GNU-ar):** a shared
> `.dylib` under rules_foreign_cc bakes the long sandbox prefix into each dylib's
> `LC_ID_DYLIB`, overflowing `cmdsize` and corrupting the Mach-O. Homebrew `ar`
> writes GNU-format members that macOS `ld64` rejects; merging via
> `/usr/bin/libtool -static` yields BSD-format the linker accepts. NEON must stay
> enabled or the aarch64 helper that defines `_ff_prefetch_aarch64` is dropped,
> leaving the symbol undefined (dyld "symbol not found"). The consumer force-loads
> the archive (see §4.9 / spike `linkopts`) so no member is lazily dropped.
>
> **Dev-host caveat:** x264 is linked from the dev host's Homebrew install
> (`pkg-config` + explicit extra flags). A hermetic build would add x264 as its own
> `rules_foreign_cc` target and point `--extra-cflags`/`--extra-ldflags` at it.

### third_party/android_ndk/BUILD.bazel

```python
# NDK media codec is provided by the Android toolchain; this wrapper
# exposes the NdkMediaCodec headers for non-Android-build consumers.
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "android_media_codec",
    hdrs = glob(["media/*.h"]),
    includes = ["."],
    visibility = ["//visibility:public"],
)
```

## 4.9 Internal Module BUILD Rules

```python
# src/framework/core/BUILD.bazel
cc_library(
    name = "core",
    srcs = glob(["*.cc"]),
    hdrs = glob(["*.h"]),
    visibility = ["//src/framework:__subpackages__", "//tests:__subpackages__"],
)

# src/framework/api/BUILD.bazel
cc_library(
    name = "api",
    srcs = glob(["*.cc"]),
    hdrs = glob(["*.h"]),
    deps = ["//src/framework/core"],
    visibility = ["//src/framework:__subpackages__", "//tests:__subpackages__"],
)

# src/framework/utils/BUILD.bazel
cc_library(
    name = "utils",
    srcs = glob(["*.cc"]),
    hdrs = glob(["*.h"]),
    deps = ["//src/framework/core"],
    visibility = ["//src/framework:__subpackages__", "//tests:__subpackages__"],
)

# src/framework/backend/android/BUILD.bazel
cc_library(
    name = "android",
    srcs = glob(["*.cc"]),
    hdrs = glob(["*.h"]),
    deps = [
        "//src/framework/core",
        "//src/framework/api",
        "//src/framework/utils",
        "@androidndk//:media_codec",
    ],
    visibility = ["//src/framework:__subpackages__", "//tests:__subpackages__"],
)

# src/framework/backend/ffmpeg/BUILD.bazel
cc_library(
    name = "ffmpeg",
    srcs = glob(["*.cc"]),
    hdrs = glob(["*.h"]),
    deps = [
        "//src/framework/core",
        "//src/framework/api",
        "//src/framework/utils",
        "@ffmpeg//:ffmpeg_codec",
    ],
    visibility = ["//src/framework:__subpackages__", "//tests:__subpackages__"],
)
```

## 4.10 Public API Target

```python
# src/framework/public/BUILD.bazel
cc_library(
    name = "video_codec",
    hdrs = glob(["include/video_codec/*.h"]),
    strip_include_prefix = "include",
    copts = [
        "-fvisibility=hidden",
        "-fvisibility-inlines-hidden",
        "-DVIDEO_CODEC_SHARED_LIBRARY",
    ],
    alwayslink = 1,
    deps = [
        "//src/framework/core",
        "//src/framework/api",
        "//src/framework/utils",
        # backend linked via select() at consumer / alias level
    ] + select({
        "//platforms:android_arm64": ["//src/framework/backend/android"],
        "//platforms:darwin_arm64":  ["//src/framework/backend/darwin"],
        "//conditions:default":      ["//src/framework/backend/ffmpeg"],
    }),
    visibility = ["//visibility:public"],
)

# Shared library for non-Bazel consumers
cc_binary(
    name = "video_codec_shared",
    srcs = ["video_codec_init.cc"],
    linkshared = True,
    linkstatic = True,
    copts = [
        "-fvisibility=hidden",
        "-fvisibility-inlines-hidden",
        "-DVIDEO_CODEC_SHARED_LIBRARY",
    ],
    deps = [":video_codec"],
    visibility = ["//visibility:public"],
)
```

---

# 5. Code Style

## 5.1 Google C++ Style

本项目严格遵守 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)。

### Naming

| Category            | Style              | Example                        |
|---------------------|--------------------|--------------------------------|
| File names          | `snake_case`       | `mediacodec_video_encoder.cc`  |
| Type/Class          | `PascalCase`       | `class MediaCodecVideoEncoder` |
| Function            | `PascalCase`       | `bool Init()`                  |
| Variable            | `snake_case`       | `int frame_count`              |
| Member variable     | `snake_case_`      | `int frame_count_`             |
| Constant            | `kPascalCase`      | `const int kMaxPlanes = 3`     |
| Namespace           | `snake_case`       | `namespace video::codec`       |
| Macro               | `UPPER_SNAKE_CASE` | `VIDEO_CODEC_API`              |

### Formatting

- Indentation: 2 spaces (no tabs)
- Line length: 80 characters (Google C++ style; enforced via `codec/.clang-format`)
- Use `nullptr`, not `NULL` or `0`
- Use `auto` sparingly, only when type is obvious
- Include order: related header, C++ standard library, third-party, project headers
- Use `//` for inline comments; `/* */` only for documentation blocks

### Ownership & Pointers

- Prefer `std::unique_ptr` over raw pointers
- Use raw pointers only for non-owning references
- Avoid `std::shared_ptr` unless ownership is truly shared

### Header Conventions

- Use `#pragma once` (no include guards)
- Headers must be self-contained (include all deps)
- `.h` / `.cc` file pairs required for each class
- Unit test files: `*_test.cc`

---

# 6. Commit Convention

## 6.1 Conventional Commits

```
<type>(<scope>): <description>

[optional body]

[optional footer]
```

### Types

| Type       | Usage                                    |
|------------|------------------------------------------|
| `feat`     | New feature                              |
| `fix`      | Bug fix                                  |
| `docs`     | Documentation only                        |
| `style`    | Code style, formatting (no logic change) |
| `refactor` | Code restructuring (no bug fix, no feature) |
| `perf`     | Performance improvement                  |
| `test`     | Adding or fixing tests                   |
| `build`    | Build system (Bazel, etc.)               |
| `ci`       | CI/CD changes                            |
| `chore`    | Maintenance, dependencies, etc.          |

### Scopes

| Scope      | Area                        |
|------------|-----------------------------|
| `core`     | Core types module           |
| `api`      | Encoder interface / factory |
| `android`  | MediaCodec backend          |
| `ffmpeg`   | FFmpeg backend              |
| `darwin`   | VideoToolbox backend        |
| `utils`    | Format conversion           |
| `public`   | Public API layer            |
| `example`  | Example demo                |
| `build`    | Bazel build files           |
| `docs`     | Documentation               |

### Examples

```
feat(core): add VideoFrame, AudioFrame, VideoPacket, AudioPacket types
feat(api): implement VideoEncoder/AudioEncoder abstract and factory
feat(android): add MediaCodecVideoEncoder and MediaCodecAudioEncoder
feat(ffmpeg): add FFmpegVideoEncoder and FFmpegAudioEncoder
feat(api): add InputSurface and NativeBuffer zero-copy input
feat(utils): add YUV420P<->NV12 and PCM format conversion
build(bazel): integrate FFmpeg via http_archive
docs: add project bootstrap doc
```

### Rules

- Description must be lowercase, imperative mood, no period
- All text must be pure ASCII (English only)
- Body explains what and why, not how
- Footer may reference issues: `Closes #123`

---

# 7. Public API Export Macro

## 7.1 VIDEO_CODEC_API

```cpp
// video_codec_export.h
#pragma once

#if defined(_WIN32)
  #if defined(VIDEO_CODEC_SHARED_LIBRARY)
    #define VIDEO_CODEC_API __declspec(dllexport)
  #else
    #define VIDEO_CODEC_API __declspec(dllimport)
  #endif
#else
  #if defined(VIDEO_CODEC_SHARED_LIBRARY)
    #define VIDEO_CODEC_API __attribute__((visibility("default")))
  #else
    #define VIDEO_CODEC_API
  #endif
#endif
```

- 编译单元统一使用 `-fvisibility=hidden`
- 仅 `VIDEO_CODEC_API` 修饰的符号被导出
- `cc_binary(linkshared=True, linkstatic=True)` 构建共享库时定义 `VIDEO_CODEC_SHARED_LIBRARY`

## 7.2 Umbrella Header

```cpp
// video_codec.h
#pragma once

#include "video_codec/video_codec_export.h"
#include "video_codec/core.h"
#include "video_codec/api.h"
#include "video_codec/utils.h"
```

外部消费者只需 `#include "video_codec/video_codec.h"`。

---

# 8. Backend Integration

## 8.1 Android — MediaCodec

通过 NDK `AMediaCodec` 接入，版本随 Android NDK 提供，不通过 `http_archive` 引入。
`src/framework/backend/android/` 子目录封装 `AMediaCodec` 配置、输入输出缓冲管理、
Surface 创建、`NativeBuffer` 消费与 SPS/PPS/关键帧标记，对外仅暴露
`video::codec::VideoEncoder` / `AudioEncoder` 接口。
项目内部其它模块 **禁止直接依赖 NDK media 头文件**，必须经由 `backend/android/` 间接使用。

## 8.2 Generic — FFmpeg

FFmpeg 6.1 release 通过 `http_archive` 拉取（见 §4.6），但**实际从源码编译**：
`third_party/ffmpeg/BUILD.bazel` 用 `rules_foreign_cc` `configure_make` 产出合并后的
`libffmpeg.a`（libavcodec + libavutil），经 `genrule` / `cc_import` / `alwayslink`
暴露给消费者，消费者通过显式 `-force_load` 拉入整个归档（见 §4.8 / §4.9）。
`src/framework/backend/ffmpeg/` 子目录封装 `AVCodecContext` / `AVFrame` / `AVPacket`，
以及 `NativeBuffer` 硬件句柄对接，对外仅暴露
`video::codec::VideoEncoder` / `AudioEncoder` 接口。
项目内部其它模块 **禁止直接依赖 FFmpeg**，必须经由 `backend/ffmpeg/` 间接使用。

## 8.3 Apple — VideoToolbox (Phase 2+, reserved)

基于 `VideoToolbox.framework`，架构位置已在 `backend/darwin/` 预留，一期不实现，后续支持。

---

# 9. Agent-Driven Development

## 9.1 Workflow

本项目使用 **opencode + spec-kit** 进行 AI-assisted 开发：

```
┌─────────────────────────────────────────────────────┐
│  1. Human: 需求描述 / Issue                          │
│  2. spec-kit: 规格化 → spec/*.yaml 或 spec/*.md      │
│  3. opencode agent: 读取 spec → 实现代码             │
│  4. agent: 自测 (bazel test //...)                   │
│  5. Human: Code Review                              │
│  6. agent: 根据 review 修改                          │
│  7. 提交 (conventional commit)                       │
└─────────────────────────────────────────────────────┘
```

## 9.2 Spec Files

规格文件位于 `spec/` 顶层目录，命名格式：

```
spec/
├── core_types.yaml           # VideoFrame/AudioFrame/Packet/NativeBuffer
├── video_encoder_api.yaml    # VideoEncoder 抽象 + Factory + Surface
├── audio_encoder_api.yaml    # AudioEncoder 抽象 + Factory
├── android_mediacodec.yaml   # MediaCodec 视频+音频后端规格
├── ffmpeg_encoder.yaml       # FFmpeg 视频+音频后端规格
├── pixel_format.yaml         # 视频格式转换规格
└── sample_format.yaml        # 音频格式转换规格
```

每个 spec 文件包含：接口签名、行为描述、边界条件、测试要点。

## 9.3 Agent Instructions

Agent 在实现时遵循：

1. 读取对应 `spec/` 文件，理解接口契约
2. 按 Google C++ Style 编写代码（2-space indent, 100-col width, etc.）
3. 每个功能带对应 `*_test.cc` 单元测试
4. 提交前运行 `bazel test //...` 验证
5. Commit message 遵循 Conventional Commits 格式

---

# 10. MVP Deliverables

一期完成后应具备：

- Core 类型库（VideoFrame, AudioFrame, Packet, NativeBuffer, enums）
- 统一 `VideoEncoder` / `AudioEncoder` 抽象接口与工厂平台选择
- Android `MediaCodecVideoEncoder` / `MediaCodecAudioEncoder` 后端（视频 H.264/HEVC + 音频 AAC，含 Surface 零拷贝）
- FFmpeg `FFmpegVideoEncoder` / `FFmpegAudioEncoder` 后端（libx264 / libx265 / AAC / Opus，含 `NativeBuffer` 硬件路径）
- 像素格式转换工具（YUV420P ↔ NV12，stride 处理）+ 采样格式转换工具
- Public API 汇总头文件与导出宏
- 编码示例（视频 CPU 帧 / InputSurface → .h264；音频 PCM → .aac）
- 单元测试覆盖 core 类型与 utils 转换
- 开发者文档

---

# 11. Success Criteria

一期完成时，应满足以下目标：

- 业务侧可通过统一 `VideoEncoder` / `AudioEncoder` 接口完成编码，不感知平台后端
- Android 平台经 `MediaCodec` 产出正确 H.264/HEVC 码流与 AAC 音频
- 非 Android 平台经 FFmpeg 产出正确 H.264/HEVC 码流与 AAC/Opus 音频
- Android 侧 `InputSurface` 直接绘制路径可产出正确码流（零拷贝）
- `NativeBuffer` 指针对象能被 MediaCodec 与 FFmpeg 硬件路径统一消费
- 像素格式转换（YUV420P ↔ NV12）与采样格式转换往返一致、无肉眼/可闻损劣
- 示例程序能完整运行并产出可播放的裸码流文件
- 可作为独立 Bazel Library 被其他项目依赖（按平台 `select()` 切换后端）
- Public API 清晰、自包含、有文档
- 测试通过率 100%

---

# 12. References

- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [Conventional Commits](https://www.conventionalcommits.org/)
- [Android NDK MediaCodec](https://developer.android.com/ndk/reference/group/media)
- [FFmpeg libavcodec](https://ffmpeg.org/doxygen/trunk/group__lavc__encoding.html)
- [VideoToolbox](https://developer.apple.com/documentation/videotoolbox) (Phase 2+)
- [native_ui - Reference Project](/Users/moks/Develop/docker/ubuntu24/codes/native_ui/native_ui)

  本项目 Bazel 配置（WORKSPACE, .bazelrc, platforms, deps.bzl）、C++ 代码规范与
  目录分层结构均参考 native_ui 的设计。
