# Data Model: Android MediaCodec 后端实现

**Branch**: `006-android-mediacodec-backend` | **Date**: 2026-08-14 | **Spec**: [spec.md](spec.md)

> 范围（澄清结论）：H.264/HEVC 视频编码（CPU 输入路径）+ AAC 音频编码 + MediaMuxer 封装后端。

## Entities

### MediaCodecVideoEncoder（Android 视频编码后端）

对 `api::VideoEncoder` 的实现，驱动系统 `AMediaCodec` 产出 H.264/HEVC 码流（CPU 输入路径）。

| Field / Method | Type | Meaning |
|----------------|------|---------|
| `config_` | `VideoConfig` | 编码配置（codec/宽高/fps/码率/input_format） |
| `codec_` | `AMediaCodec*` | 系统编码器句柄（`createEncoderByType`） |
| `format_` | `AMediaFormat*` | 编码器输入格式（含 color-format 映射） |
| `sps_/pps_` | `std::vector<uint8_t>` | `BUFFER_FLAG_CODEC_CONFIG` 缓冲（Annex-B 关键帧前置用） |
| `pts_` | `int64_t` | 输入帧时间戳计数器（微秒） |
| `Init()` | Status | 创建/配置/启动编码器 |
| `Encode(VideoFrame)` | Result<VideoPacket> | 拷贝输入缓冲 + 收输出包（push 模式转发 sink） |
| `Encode(NativeBuffer)` | Result<VideoPacket> | `kUnsupportedOperation`（v1 无零拷贝路径） |
| `CreateInputSurface()` | unique_ptr<InputSurface> | 返回空（v1，spec FR-004） |
| `Flush()` | Result<VideoPacket> | 冲刷剩余输出包 |
| `Release()` | void | stop/delete 编码器（幂等） |
| `SetOutputSink(PacketSink*)` | Status | push 模式（非拥有） |

**Validation / lifecycle**:
- 遵循 `EncoderLifecycle` 状态机（Created → Initialized → Encoding → Flushed → Released）。
- `Init()` 前 `config_.IsValid()`（宽高为正）；设备不支持请求 codec（如无 HEVC）→ `kPlatformUnsupported`。
- 输出包：`data` 为 Annex-B，`pts_us` 来自输入时间戳，`keyframe` 由 `BUFFER_FLAG_KEY_FRAME` 决定。

### MediaCodecAudioEncoder（Android 音频编码后端）

对 `api::AudioEncoder` 的实现，驱动系统 `AMediaCodec` 产出 AAC 码流。

| Field / Method | Type | Meaning |
|----------------|------|---------|
| `config_` | `AudioConfig` | 编码配置（codec/采样率/声道/码率） |
| `codec_` | `AMediaCodec*` | 系统编码器句柄（`createEncoderByType("audio/mp4a-latm")`） |
| `Init()` | Status | 创建/配置/启动编码器 |
| `Encode(AudioFrame)` | Result<AudioPacket> | 拷贝 PCM 输入 + 收输出包 |
| `Flush()` | Result<AudioPacket> | 冲刷剩余输出包 |
| `Release()` | void | stop/delete（幂等） |
| `SetOutputSink(PacketSink*)` | Status | push 模式（非拥有） |

**Validation / lifecycle**:
- 遵循 `EncoderLifecycle` 状态机；`AudioConfig::IsValid()`（采样率/声道为正）。
- 输出包：`data` 为裸 AAC 帧，`pts_us` 按采样时钟推进（每帧 `nb_samples` 对应真实时长），`keyframe=false`。

### MediaCodecMuxer（Android 封装后端）

对 `api::Muxer` 的实现，基于系统 `AMediaMuxer` 产出 MP4。写入遵循研究 R4 的"临时文件承载 + Finish 回放 ByteSink"方案。

| Field / Method | Type | Meaning |
|----------------|------|---------|
| `config_` | `MuxerConfig` | 封装配置（format/fps/audio 元数据） |
| `muxer_` | `AMediaMuxer*` | 系统封装器句柄（`AMediaMuxer_new(fd, MP4)`） |
| `tmp_fd_` | `FILE*`/fd | 可 seek 临时文件（承载 muxer 输出） |
| `sink_` | `ByteSink*` | 输出目标（非拥有，`SetOutput` 注入） |
| `track_map_` | `{媒体类型 → trackIdx}` | 轨道索引（video/audio 各一） |
| `csd_` | `{视频: sps/pps, 音频: asc}` | 各轨 codec-specific 数据（addTrack 前写入 `AMediaFormat`） |
| `SetOutput(ByteSink*)` | Status | 注入输出目标（须早于首次 Push） |
| `Push(VideoPacket&&)` | Status | 捕获 csd → addTrack/start（懒）→ writeSampleData |
| `Push(AudioPacket&&)` | Status | 同上（音频轨） |
| `Flush()` | Status | 预留（MediaMuxer 无增量 flush，kOk） |
| `Finish()` | Status | `AMediaMuxer_stop` → 回放全部字节到 `ByteSink` |
| `Release()` | void | delete muxer、关临时文件（幂等） |

**Validation rules**:
- `MuxerConfig::IsValid()`（宽高为正；音频启用时采样率/声道为正）。
- 容器懒打开：收到某轨首个 `csd`（视频 = 首个含 SPS/PPS 的关键帧包，音频 = 首个含 ASC 的包）才 `addTrack` + `start`；此前数据丢弃/缓冲（同 FFmpeg 封装"非关键帧丢弃"先例）。
- 输出投递时机：`Finish()`（一次性回放）；之前 `ByteSink` 不产生字节（与 FFmpeg 分片增量不同，见 contract）。

### AndroidRegister（注册）

`backend/android/register.cc`（`alwayslink = True`），静态初始化注册：

| 注册点 | Creator |
|--------|---------|
| `RegisterVideo(Backend::kAndroid, ...)` | `make_unique<MediaCodecVideoEncoder>` |
| `RegisterAudio(Backend::kAndroid, ...)` | `make_unique<MediaCodecAudioEncoder>` |
| `RegisterMuxer(Backend::kAndroid, ...)` | `make_unique<MediaCodecMuxer>` |

### MediaCodecUtils（可宿主单测的纯逻辑）

独立于 AMediaCodec 句柄的纯函数/映射，供宿主单测覆盖：

| Function | Meaning |
|----------|---------|
| `ColorFormatFor(PixelFormat)` | I420→19、NV12→21、其他→不支持 |
| `MimeFor(VideoCodecType)` / `MimeFor(AudioCodecType)` | `video/avc`、`video/hevc`、`audio/mp4a-latm` |
| `BuildAnnexB(spS/pps, payload, keyframe)` | 关键帧前置 SPS/PPS 的 Annex-B 组装 |
| `ExtractCsd(buffer, flag)` | 从 CODEC_CONFIG 缓冲提取 sps/pps/asc |
| `BufferSizeFor(w,h,format)` | 输入缓冲所需字节数（对齐后的平面大小） |

## Relationships

- 三个后端实体（Video/Audio/Muxer）平级，各自独立实现 `api` 抽象，通过 `AndroidRegister` 注册到 `CodecFactory`。
- `MediaCodecMuxer` 消费 `VideoPacket`/`AudioPacket`（含 csd 与关键帧标记），与 `queue.Await` 直接衔接。
- 后端仅依赖 `core`/`api`/`utils`/`mediacodec_utils` 与 libmediandk；不依赖 FFmpeg、不依赖其他后端。

## State Transitions

- 编码器：`Created → Initialized → Encoding → Flushed → Released`（`EncoderLifecycle` 复用，与 FFmpeg 后端一致）。
- 封装器：`Unopened → Opened(已 addTrack+start) → Finished(已 stop+回放)`；`Finish`/`Release` 幂等。
