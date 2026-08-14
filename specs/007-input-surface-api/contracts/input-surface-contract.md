# Contract: Input Surface（Android MediaCodec 输入面）

**Branch**: `007-input-surface-api` | **Date**: 2026-08-14 | **Spec**: [spec.md](spec.md) | **Data model**: [data-model.md](../data-model.md)

## 1. API 契约（VideoEncoder）

- **C-001**: `VideoEncoder::CreateInputSurface()` 返回 `void*`；默认实现返回 `nullptr`。返回的是**不透明原生绘制句柄**，不解释为任何框架类型。
- **C-002**: Android 后端在 `config.input_surface == true` 且 `Init()` 成功后，`CreateInputSurface()` 返回非空句柄（`ANativeWindow*` 按 `void*` 返回）；否则返回 `nullptr`。
- **C-003**: 非安卓后端（FFmpeg）`CreateInputSurface()` 恒返回 `nullptr`，不崩溃、不产生部分初始化状态（spec FR-002）。
- **C-004**: 句柄生命周期绑定编码器：有效至 `Release()`（或析构）；之后调用者不得再使用该句柄（spec FR-007）。v1 框架不 `ANativeWindow_acquire`——句柄由编码器内部持有，调用者不得 `release` 该句柄。
- **C-005**: `CreateInputSurface()` 幂等：重复调用返回同一有效句柄（不重复创建）。

## 2. 配置与输入模式

- **C-010**: `input_surface` 标志在 `VideoConfig` 声明，`Init()` 前设定；`Init()` 后修改无效（configure 期决定，研究 R1）。
- **C-011**: `input_surface == true` 时，`VideoConfig::IsValid()` MUST 校验：宽高为正且偶数、codec 为 H.264；非法配置返回 `kInvalidArgument`。
- **C-012**（输入模式互斥）: 同一编码器实例上，CPU 帧输入与输入面输入互斥：
  - surface 模式下 `Encode(VideoFrame)` / `Encode(NativeBuffer)` 返回 `kUnsupportedOperation`（spec FR-009）；
  - CPU 模式下 `CreateInputSurface()` 返回 `nullptr`。
- **C-013**: surface 模式下编码器以 `COLOR_FormatSurface`(0x7F000789) 配置（参考项目 `android_media.cc` 确认），输入像素格式/尺寸/stride 由句柄（`ANativeWindow` 缓冲）在绘制时暴露（spec FR-003）。

## 3. 绘制与投递

- **C-020**: 调用者经句柄绘制并投递：CPU 路径用 `ANativeWindow_setBuffersGeometry` + `ANativeWindow_lock` + `ANativeWindow_unlockAndPost`；缓冲内容在 lock/unlock 之间归调用者，之后归系统/编码器所有，调用者不得再访问（spec FR-004）。
- **C-021**: 每帧时间戳由调用者设定（CPU 路径 `ANativeWindow_setBuffersTimestamp`，纳秒，经 `dlsym` 运行时解析，研究 R1）；无法解析时间戳 API 时，编码器不注入时间戳（输出 PTS 由系统决定，文档化限制）。
- **C-022**: 缓冲队列与背压由系统保障（`ANativeWindow` 自带队列）；框架不引入额外无界缓冲（spec FR-005）。队列满时 lock/绘制侧按系统语义阻塞或失败。
- **C-023**（输出泵送）: 调用者每绘制一帧后 MUST 调用 `VideoEncoder::Poll()` 排空已就绪输出（push 模式交付 sink）——硬件编码器在输出未排空时停止消费输入面缓冲，否则输入面队列填满、绘制阻塞（研究 R7）。CPU 模式下 `Poll()` 为 no-op。

## 4. 输出与 EOS

- **C-030**: 输入面路径产出的 `VideoPacket` 与 `Encode(VideoFrame)` 同构（数据/PTS/keyframe），经同一 Drain/push/pull 输出管线，下游 muxer/文件无需感知来源（spec FR-006）。
- **C-031**（EOS/Flush）: surface 模式下 `Flush()` 调用 `AMediaCodec_signalEndOfInputStream` 触发 EOS，随后 drain 输出；drain 以 EOS 缓冲到达**或 deadline 超时**（默认 5s）结束（研究 R3——`signalEndOfInputStream` 后各厂商编码器的 EOS 输出行为不统一，部分设备不发 EOS 缓冲，deadline 为安卓通用兜底）。剩余帧 MUST 全部输出（spec FR-008）。
- **C-032**: surface 模式下 `Flush()` 后编码器状态可安全回收；`Release()` 幂等（先 stop/delete codec）。

## 5. 错误与边界

- **C-040**: `createInputSurface` 失败（设备不支持）→ `CreateInputSurface()` 返回 `nullptr` + 编码器进入明确错误状态（`kPlatformUnsupported`），不静默降级（spec FR-008/Edge Cases）。
- **C-041**: 首帧非关键帧——与既有 muxer 契约一致（首个关键帧前丢弃），输入面路径不改变该行为。
- **C-042**: 编码器失败/重置时在途缓冲由系统回收，后续句柄操作导向明确错误状态。
- **C-043**: 空绘制（未写入内容）按正常帧提交（不做内容校验），文档化。

## 6. 测试契约

- **C-050**: 宿主 googletest 覆盖可测纯逻辑：`VideoConfig::IsValid()` surface 校验（C-010/C-011）、格式常量（`COLOR_FormatSurface` 值等）。
- **C-051**: 设备验证（`make android-surface`）：`encode_file --surface` CPU 绘制 60 帧 RGBA SMPTE → MP4，ffprobe 双流 + ffmpeg 解码校验（复用既有 run 校验）。
- **C-052**: 宿主（非 Android）构建与全部既有测试不因本功能回归（非安卓 `CreateInputSurface()` 返回 `nullptr`，FFmpeg 后端无变化）。
- **C-053**: 输入模式互斥在设备验证：surface 模式下调用 `Encode(VideoFrame)` 返回明确错误，不崩溃。
