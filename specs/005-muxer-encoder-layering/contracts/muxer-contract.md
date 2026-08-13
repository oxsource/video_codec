# Contract: Muxer 接口（api/muxer.h）

**Branch**: `005-muxer-encoder-layering` | **Date**: 2026-08-13 | **Spec**: [spec.md](../spec.md) | **Data model**: [data-model.md](../data-model.md)

定义通用的格式封装接口。编码器（encoder）与封装器（muxer）为平级独立抽象
（参考 Android MediaCodec）；首个实现为 FFmpeg backend muxer。

## 1. 接口契约

```cpp
// api/muxer.h
#pragma once
#include <memory>
#include "core/packet_sink.h"
#include "core/result.h"
#include "core/types.h"

namespace video {
namespace codec {

class ByteSink;  // fwd-declared: `api` stays free of an `io` dependency.

// Generic muxer; every backend subclasses this. Implements PacketSink so a
// PacketSource::Await() can hand packets straight to it (queue -> muxer,
// no adapter). Container opens lazily on the first keyframe (needs SPS/PPS);
// earlier non-keyframes are dropped. Not thread-safe.
class Muxer : public PacketSink {
 public:
  ~Muxer() override = default;

  // Resolve backend + construct. Returns nullptr if no matching backend is
  // linked for this platform/config.
  static std::unique_ptr<Muxer> Create(const MuxerConfig& config);

  // Attach the byte output target (non-owning; must outlive this muxer).
  // MUST be called before the first Push(); nullptr detaches.
  virtual Status SetOutput(ByteSink* sink) = 0;

  Status Push(VideoPacket&& pkt) override = 0;
  Status Push(AudioPacket&& pkt) override {
    return Status::kUnsupportedOperation;  // v1: video-only
  }
  Status Flush() override = 0;   // flush buffered fragment bytes to the sink
  Status Finish() override = 0;  // write trailer + final commit to the sink

  virtual void Release() = 0;  // free external resources (idempotent)
};

}  // namespace codec
}  // namespace video
```

## 2. 工厂契约（api/codec_factory）

```cpp
using MuxerCreator =
    std::function<std::unique_ptr<Muxer>(const MuxerConfig&)>;
void RegisterMuxer(Backend b, MuxerCreator fn);
std::unique_ptr<Muxer> CreateMuxer(const MuxerConfig& cfg);
```

- `CreateMuxer` 经 `ResolveBackend(cfg.backend)` 选择平台后端。
- backend 在静态初始化时自注册（`register.cc`，`alwayslink` 保证链接）。
- `Muxer::Create` 转发到 `CreateMuxer`。

## 3. 配置契约（core/types.h）

```cpp
enum class MuxFormat { kMp4 };  // v1；后续：kMkv, kTs, kWebm

struct MuxerConfig {
  MuxFormat format = MuxFormat::kMp4;
  bool fragmented = true;   // fMP4
  int width = 0;
  int height = 0;
  int fps = 30;
  Backend backend = Backend::kAuto;
  bool IsValid() const { return width > 0 && height > 0; }
};
```

## 4. 生命周期与边界

- **SetOutput 前置**：首次 `Push` 前必须调用；`nullptr` 解除输出（返回 pull 语义无效，
  仅用于清理）。违反返回 `kInvalidArgument`。
- **懒打开**：首个关键帧构造容器头部（ftyp+moov）；此前非关键帧静默丢弃（`kOk`）。
- **首个关键帧交付**：一次性产出"头部 + 首分片"（FR-007 / FR-002 acceptance #2），
  保证单次 Push 后 sink 已含可读文件前缀。
- **Finish**：写容器尾部（moov/mdat 完整），随后 sink flush（最终提交）。幂等。
- **音频**：v1 视频封装，音频 Push 返回 `kUnsupportedOperation`。
- **失败语义**：sink 写错/内存不足 → `kEncodeFailed`；格式不支持/后端缺失 →
  `kUnsupportedFormat` / 工厂返回 `nullptr`。不产生损坏输出。

## 5. 依赖约束

- `api → core` 不变（`PacketSink` 迁 core；`ByteSink` 前向声明）。
- 仅 `backend/*` 可依赖 `@ffmpeg`；`mux → @ffmpeg` 违规边随本功能消除。
- 新边 `backend/ffmpeg → {api:muxer, core, io:byte_sink, @ffmpeg}` 无环。

## 6. Acceptance

- **A1**: `Muxer::Create(cfg)` 返回可用实例；`SetOutput` 后 `Push` 关键帧 → sink 首字节
  为 `ftyp`（0x66 0x74 0x79 0x70）。
- **A2**: `Finish()` 后输出为合法 MP4（含 moov/mdat），标准工具可识别播放。
- **A3**: 首个非关键帧被丢弃；首关键帧产出"头+首分片"单次交付。
- **A4**: `Push(AudioPacket&&)` 默认返回 `kUnsupportedOperation`。
- **A5**: 无 `SetOutput` 即 `Push` 返回 `kInvalidArgument`；`Release()` 幂等。
- **A6**: 移除 `Mp4Muxer`/`Mp4Consumer` 对外组件后，example 与现有测试仍编译运行。
