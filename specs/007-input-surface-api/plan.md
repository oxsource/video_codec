# Implementation Plan: Input Surface（Android MediaCodec 输入面）

**Branch**: `007-input-surface-api` | **Date**: 2026-08-14 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/007-input-surface-api/spec.md`

## Summary

在既有 api 抽象（`VideoEncoder`）+ backend 实现基础上，为 Android 后端打通**系统硬件输入面**（MediaCodec `createInputSurface`）：`VideoEncoder::CreateInputSurface()` 改为直接返回原生句柄 `void*`（Android 上为 `ANativeWindow*`，零拷贝），删除 `InputSurface` 类与 `input_surface.h`。调用者向该句柄绘制（CPU `ANativeWindow_lock` 或 GPU EGL），系统自动投递缓冲给硬件编码器；`Flush()` 经 `AMediaCodec_signalEndOfInputStream` 触发 EOS 并冲刷剩余输出。非安卓后端保持返回 `nullptr`（明确不支持）。输入面路径与 CPU 帧输入（`Encode(VideoFrame)`）在同一编码器实例上互斥。示例 `encode_file` 增加 `--surface` 模式（Android-only，CPU 绘制 RGBA SMPTE 进窗口）用于设备验证；`android_codec.sh` 增加 surface 验证步骤。

## Technical Context

**Language/Version**: C++17（spec 001 确立）

**Build System**: Bazel 6.5.0；Android 交叉编译 `--config android_arm64`（target API 31）

**Primary Dependencies**: NDK `libmediandk`（`AMediaCodec_createInputSurface`/`signalEndOfInputStream`，头文件已确认）+ `libandroid`（`ANativeWindow_lock`/`setBuffersGeometry`/`unlockAndPost`，头文件与符号已确认）。参考实现：`native_ui/examples/android_media.cc`（surface 输入编码器封装）与 `external_image_demo.cc`（EGL/GLES 渲染进输入面）。

**Storage**: N/A（library，无持久存储）

**Testing**: googletest（宿主单测：VideoConfig surface 校验、互斥状态）+ 交叉编译门禁（`make android-codec`）+ 设备运行验证（`make android-surface`：CPU 绘制 → 产出 MP4 → ffprobe/解码校验）

**Target Platform**: Android arm64（cross-build + 真机验证）；宿主 macOS/Linux 回归不受影响（非安卓返回 `nullptr`）

**Project Type**: C++ static/shared library with a public C++ API

**Performance Goals**: 输入面为硬件零拷贝路径；绘制侧与编码侧流水线并行，帧率≥30fps（640x480，设备实测校准）

**Constraints**:
- `api` MUST NOT 依赖 `queue`/`io`/`consumer`/`backend/*`；后端经工厂注册接入
- 非安卓后端 `CreateInputSurface()` MUST 返回 `nullptr`（明确不支持，不崩溃、不部分初始化）
- Android 后端仅参与 Android 构建（`target_compatible_with` + `select()`），宿主 NDK-free
- 输入面与 CPU 帧输入在同一编码器实例上**互斥**（MediaCodec 语义约束：`createInputSurface` 后 input buffers 不可用）
- 时间戳机制：NDK 25.2 头文件/库**未导出** `ANativeWindow_setBuffersTimestamp`（libandroid）与 `AMediaCodec_setInputSurfaceTimestamp`（libmediandk）→ 需 `dlsym` 运行时解析（API 26+/30+ 平台符号，见研究 R1）
- 输入面路径的 EOS 语义为**安卓通用要求**：`signalEndOfInputStream` 后各厂商编码器对 EOS 输出缓冲行为不统一（部分不发），drain 必须带 deadline 兜底（研究 R3），不得死等
- surface 模式需在 `configure` 时使用 `COLOR_FormatSurface`(0x7F000789)（参考项目确认），故输入面须在 `VideoConfig` 声明（`input_surface` 标志），而非事后切换
- 既有 FFmpeg 后端与宿主测试 MUST 不受影响

**Scale/Scope**: 新增/修改：`api/video_encoder.h`（`CreateInputSurface` → `void*`）、删除 `api/input_surface.h`、`core/types.h`（`VideoConfig.input_surface`）、`backend/android/mediacodec_video.{h,cc}`（surface 模式）、示例 `encode_file.cc`（`--surface`，Android-only）、`android_codec.sh`/`mk/android.mk`（surface 验证）；宿主单测 + 设备验证

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Constitution 文件（`.specify/memory/constitution.md`）为占位模板，无项目特定原则/约束/门禁（与 004/005/006 一致，门禁为空 vacuous）。项目有效约束为已确立的架构约定（module-dependencies：api→core、backend 单外部依赖、后端互不依赖；build：one cc_library per module、alwayslink、visibility）+ 本功能 spec/contract：
- 本计划合规——不新增模块（复用 `backend/android`），只改 `api`/`core`/`backend/android`/示例/验证脚本；`api` 仅改一个方法签名（`CreateInputSurface` 返回类型）并删除 `input_surface.h`；`backend/android` 仅新增 libandroid 依赖边（→ libmediandk + libandroid，无环）；宿主依赖不变。

## Project Structure

### Documentation (this feature)

```text
specs/007-input-surface-api/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/
│   └── input-surface-contract.md   # Phase 1 output
└── tasks.md             # Phase 2 output (/speckit.tasks command - NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
codec/
├── src/framework/api/
│   ├── video_encoder.h        # CreateInputSurface() 返回 void*（默认 nullptr）；删 InputSurface 前向声明
│   └── input_surface.h        # 删除（InputSurface 类移除，API 最小化）
├── src/framework/core/types.h # VideoConfig 增加 input_surface 标志（surface 模式声明）
├── src/framework/backend/android/
│   ├── mediacodec_video.{h,cc}  # surface 模式：COLOR_FormatSurface 配置 → createInputSurface →
│   │                            #   CreateInputSurface() 返回 ANativeWindow*(void*)；CPU 输入互斥；
│   │                            #   Flush → signalEndOfInputStream + drain（带 deadline）
│   └── BUILD.bazel             # mediandk + landroid 链接（surface 路径用 ANativeWindow 系列）
├── src/examples/
│   ├── encode_file.cc          # 增加 --surface 模式（__ANDROID__ 守卫）：CPU 绘制 RGBA SMPTE
│   │                           #   进窗口（ANativeWindow_lock + dlsym 时间戳 + unlockAndPost）
│   └── BUILD.bazel             # Android 下链接 landroid + libEGL（若需）
└── tests/
    ├── api/muxer_contract_test.cc      # （如有）CreateInputSurface 契约测试
    └── utils/mediacodec_utils_test.cc  # 宿主单测：surface 配置校验等纯逻辑
```

**Structure Decision**: 沿用框架既有"api 抽象 + backend 实现"分层。接口变更最小化：`CreateInputSurface()` 返回类型改 `void*`、删除 `InputSurface` 类（对应 spec FR-010）；surface 声明放在 `VideoConfig`（因 `COLOR_FormatSurface` 需 configure 期决定，研究 R1）；Android 实现全部落在既有 `mediacodec_video` 模块；示例沿用 `encode_file` 单 binary 模式（`--surface`，Android-only 守卫）；验证复用 `android_codec.sh`。

## Complexity Tracking

> 本功能不引入超出既有模式的复杂度：无新构建范式、无跨 backend 依赖、无新抽象层次。
> `CreateInputSurface()` 返回 `void*` 是 spec 澄清明确的最小接口；时间戳经 `dlsym` 运行时解析是
> NDK 25.2 头文件/库缺口的适配（研究 R1），drain 带 deadline 是 EOS 行为跨设备不统一的安卓通用兜底（研究 R3），
> 均为有理据的最小方案，记录于 research/contract。Constitution Check 无违规，本表留空。

## Phase 0: Research

见 `research.md`——解析关键决策：输入面 API 形态与时间戳机制（R1）、CPU 绘制路径与格式（R2）、EOS/Flush 语义（R3）、输入模式互斥（R4）、示例/验证策略（R5）、构建接线（R6）。

## Phase 1: Design & Contracts

见 `data-model.md`（VideoConfig.surface 标志、MediaCodecVideoEncoder surface 状态）、`contracts/input-surface-contract.md`（实现与行为契约，含互斥、句柄生命周期、时间戳限制）、`quickstart.md`（Android 构建/运行与最短调用）。Phase 1 完成后更新 `CODEBUDDY.md` 的 `<!-- SPECKIT -->` 引用至本 plan.md。
