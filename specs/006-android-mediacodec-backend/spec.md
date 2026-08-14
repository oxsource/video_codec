# Feature Specification: Android MediaCodec 后端实现

**Feature Branch**: `006-android-mediacodec-backend`

**Created**: 2026-08-14

**Status**: Draft

**Input**: User description: "新建提案，在当前架构基础上完成安卓mediacodec的backend实现"

## Clarifications

### Session 2026-08-14

- Q: 示例形态——改造 `ffmpeg_encode_file` 支持 backend 参数，binary 是否改名？ → A: 改名为 `encode_file`（backend 无关，BUILD target/文档同步更新）
- Q: 输出文件名如何体现 backend？ → A: 统一追加 `-<backend>` 后缀，backend 用 `BackendToString` 规范名（如 `out-ffmpeg.mp4`、`out-android.mp4`）
- Q: `--backend` 参数缺省时默认值？ → A: `auto`（平台选择：Android→mediacodec、宿主→ffmpeg）

## User Scenarios & Testing *(mandatory)*

<!-- 本功能面向 video_codec 库的使用者（应用开发者），
     用户旅程按价值从高到低排序，每个旅程可独立交付与测试。 -->

### User Story 1 - Android 设备上通过统一 API 使用系统硬件编码 (Priority: P1)

应用开发者在 Android 设备上部署应用时，希望使用与桌面/Linux 完全一致的框架调用代码完成视频编码（H.264），由框架自动选择并驱动设备自带的系统媒体编码能力，应用无需了解底层编码器细节，也无需按平台维护两套代码。

**Why this priority**: 框架已确立"api 抽象 + backend 实现"与运行时后端选择（`Backend::kAuto` 在 Android 构建上解析为 `kAndroid`），但当前 `backend/android` 为空壳——`CodecFactory::Create*` 在 Android 上解析到 `kAndroid` 却无注册实现、返回空。补齐该后端是"统一 API 跨平台可用"这一核心承诺在 Android 上的关键缺口，也是本功能的主体价值。

**Independent Test**: 在 Android 设备/模拟器上运行框架示例，用与桌面相同的调用代码完成 H.264 编码，得到可被标准播放器识别的输出；上层调用代码不含任何平台分支。

**Acceptance Scenarios**:

1. **Given** 应用运行在 Android 设备上且配置为 `Backend::kAuto`，**When** 开发者创建视频编码器并编码若干帧，**Then** 框架自动选择 Android 后端，成功产出 H.264 码流
2. **Given** 开发者显式请求 `Backend::kAndroid`，**When** 在非 Android 构建（宿主）上创建编码器，**Then** 框架返回明确的"不支持"错误，不产生损坏或静默错误输出
3. **Given** 编码过程结束，**When** 开发者冲刷（flush）编码器，**Then** 剩余码流完整输出，包顺序与时间戳正确，下游可正常消费

---

### User Story 2 - Android 音频编码与声像合成 (Priority: P2)

应用开发者在 Android 上需要 AAC 音频编码并能与视频合成，希望使用与桌面一致的音频编码器 API（push 模式 + 队列），直接复用既有声像合成（SMPTE 画面 + 测试音）流程。

**Why this priority**: 视频与音频编码器是框架的对称能力；仅视频无音频则声像合成类应用在 Android 上无法端到端工作，音频后端与视频后端应一并补齐。

**Independent Test**: 在 Android 上运行与桌面相同的声像合成流程（视频+音频 → queue → 封装 → MP4），输出含音视频双轨的合法文件。

**Acceptance Scenarios**:

1. **Given** 音频编码器配置为 AAC 且启用 push 模式，**When** 送入 PCM 帧，**Then** 输出 AAC 包经队列送达消费端，包序正确
2. **Given** 视频与音频同时编码并送入同一队列，**When** 经封装层输出文件，**Then** 文件含视频与 AAC 音频双轨

---

### User Story 3 - Android 上通过统一 Muxer API 使用系统原生封装 (Priority: P2)

应用开发者在 Android 上完成编码后，希望用与桌面一致的封装 API 把已编码的视频/音频码流封装为 MP4 文件。由于 Android 构建只链接 Android 后端，桌面的 FFmpeg 封装后端在 Android 上不可用；框架需要在 Android 提供基于系统原生封装能力（MediaMuxer 系）的封装后端，使"编码 → 封装 → 文件"全链路在 Android 与桌面行为一致。

**Why this priority**: 没有原生封装后端，Android 上就无法产出可播放的 MP4 文件，声像合成流程无法端到端闭环——封装与音频编码是让"统一 API 跨平台可用"成立的一对互补能力。

**Independent Test**: 在 Android 上运行与桌面相同的声像合成流程，经统一 Muxer API 产出含音视频双轨的 MP4，可被标准播放器播放。

**Acceptance Scenarios**:

1. **Given** 封装器配置为 MP4 且使用 `Backend::kAuto`，**When** 在 Android 上接收已编码的视频与音频包，**Then** 输出合法、可播放的 MP4 文件
2. **Given** 封装过程结束，**When** 开发者显式结束封装，**Then** 容器尾部完整写入，输出文件结构合法

---

### Edge Cases

- 目标设备缺少请求的编码格式（如机型无 HEVC 编码器）时，如何向调用方反馈明确的不支持错误？
- 系统编码器输出配置数据（SPS/PPS、AudioSpecificConfig）与首个关键帧的时序关系，如何保证下游封装层能正确构造容器头部？
- 编码器需重配（分辨率/码率变更触发系统 flush/release）时，队列中未消费的输入与已输出包如何处理？
- 封装器收到首个关键帧之前的数据时，如何处理（丢弃/缓冲），保证不产生损坏输出？
- 宿主（非 Android）构建显式请求 `kAndroid` 时，如何保证不拉入 NDK 依赖且返回明确错误？
- 示例在 Android 上显式指定宿主专用 backend（如 `--backend ffmpeg`）时，如何反馈明确错误而非损坏输出？

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: 系统 MUST 在 Android 平台提供基于系统媒体编码能力的视频编码后端，支持 H.264 编码；HEVC 作为设备能力可选（设备不支持时 MUST 返回明确的不支持错误）
- **FR-002**: 系统 MUST 在 Android 平台提供基于系统媒体编码能力的音频编码后端，支持 AAC 编码
- **FR-003**: Android 视频/音频编码后端 MUST 完整实现框架 `VideoEncoder`/`AudioEncoder` 抽象接口（`Init`/`Encode`/`Flush`/`Release`、push 模式 `SetOutputSink`），并遵循既有生命周期状态机与错误码契约
- **FR-004**: Android 视频编码 MUST 支持 CPU 输入路径（向系统编码器输入缓冲拷贝像素数据）；Surface 零拷贝输入路径不在 v1 范围（`CreateInputSurface()` 保持返回空），留作后续增强
- **FR-005**: 系统 MUST 在 Android 平台提供基于系统原生封装能力的封装后端（MediaMuxer 系），实现框架 `Muxer` 抽象、产出合法 MP4，并自注册到框架工厂
- **FR-006**: Android 后端 MUST 在 `Backend::kAuto` 的 Android 构建上被自动选中；宿主构建显式请求 `kAndroid` MUST 返回明确的"不支持"，不产生损坏输出
- **FR-007**: Android 后端 MUST 以静态初始化自注册到框架工厂（与 FFmpeg 后端同机制），使 api 层保持零依赖 backend
- **FR-008**: Android 后端 MUST 仅参与 Android 目标平台构建（宿主保持 NDK-free），其构建/测试失败不阻塞其他平台
- **FR-009**: 编码输出 MUST 携带正确的包序、时间戳与关键帧标记，满足下游队列/封装/消费端契约
- **FR-010**: 现有 FFmpeg 后端行为、框架默认行为与既有测试 MUST 不受本功能影响
- **FR-011**: 系统 MUST 提供 backend 无关的可运行示例 `encode_file`（由 `ffmpeg_encode_file` 改造并改名），通过命令行参数选择编码/封装 backend（缺省 `auto`，即平台选择：Android→mediacodec、宿主→ffmpeg），使 Android（MediaCodec）与宿主（FFmpeg）共用同一示例，示例构建配置需按平台条件化（FFmpeg 归档仅宿主链接）
- **FR-012**: 示例 `encode_file` MUST 按所用 backend 生成输出文件名，统一追加 `-<backend>` 后缀（`BackendToString` 规范名，如 `out-ffmpeg.mp4`、`out-android.mp4`），供验收断言稳定引用

### Key Entities *(include if feature involves data)*

- **Android 视频编码后端**: 在 Android 平台对 `VideoEncoder` 抽象的实现，驱动系统媒体编码能力产出压缩视频码流（H.264/HEVC，CPU 输入路径）
- **Android 音频编码后端**: 对 `AudioEncoder` 抽象的实现，产出 AAC 压缩音频码流
- **Android 封装后端**: 对 `Muxer` 抽象的实现，基于系统原生封装能力（MediaMuxer 系）产出 MP4 容器字节流
- **输入路径**: 编码器接受未压缩数据的通道——v1 为 CPU 路径（像素/PCM 数据拷贝）；Surface 零拷贝路径为后续增强
- **编码输出包**: 携带时间戳与关键帧标记的压缩码流单元，供队列/封装/消费端消费
- **后端注册与选择**: 后端静态自注册 + 构建期平台选择/运行时后端解析机制，使上层调用代码平台无关

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 在 Android 设备上，开发者使用与桌面完全相同的框架调用代码即可完成视频编码并得到可播放输出，上层无需任何平台分支（平台适配量为 0）
- **SC-002**: Android 硬件编码在同等质量设定下的端到端编码耗时相对软件编码路径显著降低（默认目标 ≥50%，以设备实测为准、可在计划阶段校准）
- **SC-003**: 视频/音频编码、push 模式、队列与封装全链路在 Android 与桌面行为一致（同一测试用例双端通过）
- **SC-004**: 宿主（非 Android）构建与全部既有测试不因本功能回归（现有测试保持通过）
- **SC-005**: Android 原生封装后端产出的 MP4 文件 100% 通过标准媒体工具的结构与可播放性校验

## Assumptions

- 框架"api 抽象 + backend 实现 + 运行时后端选择"模式延续；Android 后端作为与 FFmpeg 平级的独立 backend 子目录实现
- Android 后端基于系统媒体编码/封装能力（NDK 媒体 API）；NDK 仓库注册（`android_ndk_repository`）与 `//third_party/android_ndk:android_media_codec` 的接线是本功能的前置依赖，需在功能范围内完成
- 编码器默认行为（不启用 push 时输出裸码流）保持不变；`Backend::kAuto` 在 Android 上解析为 `kAndroid`（现状已确立）
- 编码输出码流格式与桌面一致（H.264 Annex-B 等），供既有封装/消费端直接消费
- Android 编码后端不引入 FFmpeg 依赖（架构不变式：backend 仅依赖自己的外部依赖）
- v1 范围（澄清结论）：视频编码（CPU 输入路径，H.264 必备/HEVC 设备可选）+ AAC 音频编码 + Android 原生封装（MediaMuxer 系）；Surface 零拷贝输入路径与 Opus 音频不在 v1 范围，v1 中 `CreateInputSurface()` 保持返回空，留作后续增强
