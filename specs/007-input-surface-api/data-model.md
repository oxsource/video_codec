# Data Model: Input Surface（Android MediaCodec 输入面）

**Branch**: `007-input-surface-api` | **Date**: 2026-08-14 | **Spec**: [spec.md](spec.md)

## 变更实体

### VideoConfig（新增字段）

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `input_surface` | `bool` | `false` | 声明 surface 输入模式：`true` 时编码器用系统硬件输入面（`COLOR_FormatSurface` + `createInputSurface`），CPU 帧输入被拒绝（互斥，FR-009） |

**Validation（`IsValid()` 扩展）**:
- `input_surface = true` 时：宽高为正且为偶数（编码器输入面几何要求）；`codec` 为 H.264（v1 仅验证 H.264 路径，HEVC 设备支持与否沿用既有 `kPlatformUnsupported` 语义）。
- 与 `input_format` 的关系：surface 模式下 `input_format` 不用于配置（以 `COLOR_FormatSurface` 为准），但保留用于文档/一致性校验。

### MediaCodecVideoEncoder（surface 模式新增成员）

| Field | Type | Meaning |
|-------|------|---------|
| `surface_window_` | `ANativeWindow*` | `createInputSurface` 返回的输入面句柄（v1 框架不 acquire；有效至 `Release()`，FR-007） |
| `surface_mode_` | `bool` | = `config_.input_surface`；决定 Init 配置、`Encode` 互斥、`Flush` EOS 路径 |

**状态流转（表面状态机，叠加在既有 `EncoderLifecycle` 之上）**:

```text
             input_surface=false (CPU 模式，既有路径)
             input_surface=true (surface 模式):
  Created --Init()--> Initialized(configure: COLOR_FormatSurface + createInputSurface)
                     --(surface mode 标记 Encoding)--> SurfaceReady(surface_window_ 可返回)
                     --CreateInputSurface()--> 返回 void* (ANativeWindow*)
                     --Encode(VideoFrame)--> kUnsupportedOperation (互斥, FR-009)
  SurfaceReady --绘制+Poll()泵送--> 持续编码（背压：输出未排空则编码器停止消费输入面，研究 R7）
              --Flush()--> signalEndOfInputStream -> Drain(deadline) -> Flushed
  --Release()--> Released (窗口句柄失效)
```

**关键约束**:
- `input_surface` 在配置期（Init 前）决定，不可事后切换（研究 R1：`COLOR_FormatSurface` 需 configure 期）。
- surface 模式下 `Encode(VideoFrame)`/`Encode(NativeBuffer)` 均返回 `kUnsupportedOperation`（互斥，FR-009）。
- `Flush()` 经 `AMediaCodec_signalEndOfInputStream` 触发 EOS；drain 带 deadline（默认 5s，研究 R3——各厂商编码器对 EOS 输出缓冲行为不统一，deadline 是安卓通用兜底）。
- `CreateInputSurface()` 幂等：重复调用返回同一句柄（或已创建标记）。

## 既有实体（不受影响）

- **VideoPacket**: 输入面投递帧编码后的输出包，与 `Encode(VideoFrame)` 产出同构（data/PTS/keyframe），消费端无需感知来源（FR-006）。
- **EncoderLifecycle**: 既有状态机不变；surface 模式仅增加配置与输入路径分支。

## 删除实体

- **InputSurface 类 / `api/input_surface.h`**: 删除（FR-010）。`VideoEncoder::CreateInputSurface()` 返回 `void*`。
