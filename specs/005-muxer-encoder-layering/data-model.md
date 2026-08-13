# Data Model: Muxer 与 Encoder 分层设计

**Branch**: `005-muxer-encoder-layering` | **Date**: 2026-08-13 | **Spec**: [spec.md](spec.md)

## Entities

### Muxer（封装器抽象）

api 层通用接口，继承 `core::PacketSink`。编码器与封装器平级独立（参考 Android
MediaCodec）。

| Field / Method | Type | Meaning |
|----------------|------|---------|
| `Create(config)` | static | 工厂入口，转发 `CreateMuxer` |
| `SetOutput(ByteSink*)` | virtual | 注入字节输出目标（非拥有，须早于首次 Push） |
| `Push(VideoPacket&&)` | virtual override | 消费一个已编码视频包（懒打开容器，非关键帧丢弃） |
| `Push(AudioPacket&&)` | virtual override | 默认 `kUnsupportedOperation`（v1 仅视频） |
| `Flush()` | virtual override | 刷新缓冲分片字节到 sink |
| `Finish()` | virtual override | 写容器尾部并最终提交 |
| `Release()` | virtual | 释放外部资源（幂等） |

### MuxerConfig（配置）

```cpp
enum class MuxFormat { kMp4 };  // v1；后续：kMkv, kTs, kWebm

struct MuxerConfig {
  MuxFormat format = MuxFormat::kMp4;
  bool fragmented = true;   // fMP4: header upfront + per-keyframe fragments
  int width = 0;            // 流元数据；SPS 解析失败的兜底
  int height = 0;
  int fps = 30;
  Backend force_backend = Backend::kAuto;

  bool IsValid() const { return width > 0 && height > 0; }
};
```

**Validation rules**:
- `IsValid()`：宽高必须为正（backend Init 前拒绝，同 Video/AudioEncoderConfig 先例）。
- `format` 白名单由 backend 校验：v1 仅 `kMp4`，否则 `kUnsupportedFormat`。

### MuxerBackend（封装后端）

特定平台对 `Muxer` 的实现，通过 `encoder_factory` 注册表自注册。

| Attribute | Meaning |
|-----------|---------|
| `backend` | 归属平台（首个：`Backend::kFFmpeg`） |
| `creator` | `std::function<std::unique_ptr<Muxer>(const MuxerConfig&)>` |
| 依赖 | `api:muxer`, `core`, `io:byte_sink`, `@ffmpeg`（libavformat） |

## Relationships / Wiring

```text
encoder(VideoEncoder) ──SetOutputSink──▶ queue(PacketQueue)
                                            │ PacketSource::Await(*muxer)
                                            ▼
                                    muxer(Muxer : PacketSink)
                                            │ SetOutput(ByteSink*)
                                            ▼
                                    io::ByteSink (file/stream/tee)
```

- 编码器不感知 muxer：输出仍为 `VideoPacket`（Annex-B 裸流），经 PacketQueue 传递。
- muxer 不感知编码器：作为 PacketSink 消费包，字节经 ByteSink 输出。
- 接线由调用方组装（quickstart.md）；`VideoEncoderConfig` 与 `MuxerConfig` 无引用关系。

## State Transitions

### Muxer

```text
[Created] ──SetOutput(sink)──▶ [Ready] ──Push(keyframe)──▶ [Opened]
                                                              │
   [Opened] ──Push(non-key)──▶ [Opened]  (丢弃或入当前分片)
   [Opened] ──Push(keyframe)──▶ [Opened]  (提交一分片到 sink)
   [Opened] ──Finish()───────▶ [Finished] (写尾部 + 最终提交)
   [*] ──Release()──────────▶ [Released] (幂等)
```

- 容器在**首个关键帧**懒打开（需 SPS/PPS 构造 avcC extradata）；此前的非关键帧丢弃
  （FR-007）。
- `Finish()` 可安全调用一次；`Release()` 幂等。
- `Push` 失败（sink 写错、内存不足）返回 `Status::kEncodeFailed`，调用方停止。
