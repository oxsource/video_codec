# Implementation Plan: Android MediaCodec 后端实现

**Branch**: `006-android-mediacodec-backend` | **Date**: 2026-08-14 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/006-android-mediacodec-backend/spec.md`

## Summary

在当前架构（api 抽象 + backend 实现 + 运行时后端选择）基础上，补齐 Android 后端：
`backend/android` 实现 `VideoEncoder`（H.264/HEVC，CPU 输入路径）、`AudioEncoder`（AAC）
与 `Muxer`（MediaMuxer 系）三类能力，经 `register.cc`（`alwayslink`）静态注册到
`CodecFactory`，使 `Backend::kAuto` 的 Android 构建自动使用系统媒体编码/封装能力，
上层调用代码与桌面完全一致。前置项：注册 `android_ndk_repository` 并接线
`//third_party/android_ndk:android_media_codec`（链接 libmediandk）。同时将示例
`ffmpeg_encode_file` 改造并改名为 `encode_file`（FR-011/012）：支持 `--backend` 参数
（缺省 `auto`，平台选择）、输出文件名统一带 `-<backend>` 后缀，使宿主（FFmpeg）与
Android（MediaCodec）共用同一示例，FFmpeg 归档仅在宿主构建链接。

## Technical Context

**Language/Version**: C++17（spec `001-project-scaffold` 确立）

**Build System**: Bazel 6.5.0；Android 交叉编译 `--config android_arm64`（`//platforms:android_arm64_platform`）

**Primary Dependencies**: NDK `libmediandk`（`AMediaCodec`/`AMediaFormat`/`AMediaMuxer`，
NDK 25.2.9519653 头文件已确认）；`@ffmpeg` 仅宿主侧；googletest（宿主单测）

**Storage**: N/A (library project, no persistent storage)

**Testing**: googletest（宿主单测 `mediacodec_utils` 纯逻辑）+ 交叉编译门禁（`make android-verify`）+ 设备/模拟器示例运行（复用 `mediacodec_spike` 验证模式）

**Target Platform**: Android arm64（cross-build，v1）；宿主 macOS ARM64 / Linux x86_64（回归不受影响）

**Project Type**: C++ static/shared library with a public C++ API

**Performance Goals**: 硬件编码端到端耗时相对软件路径降低 ≥50%（SC-002，设备实测校准）；零拷贝 Surface 路径不在 v1

**Constraints**:
- `api` MUST NOT depend on `queue`/`io`/`consumer`/`backend/*`——后端经工厂注册接入
- 后端仅依赖自己的外部依赖（libmediandk），不引入 FFmpeg（架构不变式）
- Android 后端仅参与 Android 构建（`target_compatible_with` + `select()`），宿主 NDK-free（FR-008）
- 注册用 `alwayslink = True`（否则 `Create*` 返回空，同 FFmpeg 先例）
- `AMediaMuxer_new` 只接受可 seek 的 fd（moov 在 stop 回写）→ 临时文件承载 + Finish 回放 ByteSink（研究 R4）
- 编码器/封装器生命周期与 push/pull 契约沿用既有 `EncoderLifecycle` 与 packet_sink 契约
- 现有 FFmpeg 后端与宿主测试 MUST 不受影响（FR-010）

**Scale/Scope**: 新增 `backend/android/{mediacodec_video, mediacodec_audio, mediacodec_muxer, mediacodec_utils, register}.{h,cc}`（命名与 FFmpeg 后端 `ffmpeg_video/ffmpeg_audio/ffmpeg_muxer` 保持一致）；接线 `WORKSPACE`/`third_party/android_ndk`；宿主单测 + 交叉编译门禁；示例 `encode_file` 改造（原 `ffmpeg_encode_file`：改名 + `--backend` 参数 + 输出命名，FR-011/012）并做 Android 运行验证

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Constitution file（`.specify/memory/constitution.md`）为占位模板，无项目特定原则/约束/门禁；
与既有 spec（004/005）一致，宪法门禁为空（vacuous）。项目有效约束为已确立的架构约定
（`codec/doc/architecture/module-dependencies.md`：api→core、backend 单外部依赖、后端互不依赖；
build 约定：one cc_library per module、register 用 alwayslink、visibility）+ 本功能 spec/contract：
本计划合规——新增后端仅新增 `backend/android` 依赖边（→core/api/utils/libmediandk，无环），
宿主依赖不变，无 FFmpeg 引入。

## Project Structure

### Documentation (this feature)

```text
specs/006-android-mediacodec-backend/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output (/speckit.plan command)
├── data-model.md        # Phase 1 output (/speckit.plan command)
├── quickstart.md        # Phase 1 output (/speckit.plan command)
├── contracts/
│   └── android-backend-contract.md   # Phase 1 output (/speckit.plan command)
└── tasks.md             # Phase 2 output (/speckit.tasks command - NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
codec/
├── WORKSPACE                          # + android_ndk_repository(name="androidndk")（R1 接线）
├── third_party/android_ndk/BUILD.bazel # android_media_codec 目标 → 链接 libmediandk（R1 接线）
├── src/framework/backend/android/     # 本功能主体
│   ├── mediacodec_video.{h,cc}           # MediaCodecVideoEncoder（CPU 路径，H.264/HEVC）
│   ├── mediacodec_audio.{h,cc}           # MediaCodecAudioEncoder（AAC）
│   ├── mediacodec_muxer.{h,cc}           # MediaCodecMuxer（AMediaMuxer + 临时文件回放）
│   ├── mediacodec_utils.{h,cc}           # 纯逻辑：color-format/mime 映射、Annex-B/csd 组装
│   ├── register.cc                       # alwayslink 静态注册（video/audio/muxer）
│   └── BUILD.bazel                       # 现有 glob 自动收纳 + register target
├── src/spike/mediacodec_spike.cc     # （已有）视频编码循环参考，扩展/复用验证
├── src/examples/encode_file.cc       # （改名）ffmpeg_encode_file → encode_file：--backend 参数 + 输出命名
│   └── BUILD.bazel                   # 平台条件化：FFmpeg 归档（data/linkopts）仅宿主链接
└── tests/
    ├── utils/mediacodec_utils_test.cc   # 宿主单测（纯逻辑）
    └── backend/android/                 # （v1 设备验证项，见 C-051；宿主无法运行）
```

**Structure Decision**: 沿用框架既有"api 抽象 + backend 实现"分层与 FFmpeg 后端的目录
约定（每模块一个 `cc_library`，`register.cc` 聚合 + `alwayslink`）。Android 专用目标
全部落在 `backend/android/`，宿主不链接（既有的 `target_compatible_with` + `select()`
已就绪）。NDK 接线为两个小改动（WORKSPACE + third_party 薄封装）。示例 `encode_file`
（原 `ffmpeg_encode_file`）改造成 backend 无关的单 binary：命令行 `--backend` 参数
（缺省 `auto`）选择编码/封装后端，输出文件名追加 `-<backend>` 后缀；其 BUILD 的
FFmpeg 归档 `data`/`-Wl,-force_load` 需按平台 `select()` 条件化，使同一目标可在
Android 交叉编译。

## Complexity Tracking

> 本功能不引入超出既有模式的复杂度：无新构建范式（沿用 select/alwayslink）、无跨
> backend 依赖、无新抽象层次。`AMediaMuxer` 的 fd 契约引入"临时文件承载 + Finish 回放"
> 这一适配（研究 R4），是有理据的最小方案（pipe 不可 seek、要求具体 sink 破坏契约），
> 记录于 research/contract。Constitution Check 无违规，本表留空。

## Phase 0: Research

见 `research.md`——解析关键决策：NDK 仓库注册与 libmediandk 接线（R1）、MediaCodec
视频/音频 CPU 路径 API 形态（R2/R3）、AMediaMuxer 与 ByteSink 契约的适配方案（R4）、
注册与构建接线（R5）、测试策略（R6）。

## Phase 1: Design & Contracts

见 `data-model.md`（后端实体与字段）、`contracts/android-backend-contract.md`（实现与
行为契约，含 C-035 行为差异记录）、`quickstart.md`（Android 构建/运行与最短调用）。
Phase 1 完成后更新 `CODEBUDDY.md` 的 `<!-- SPECKIT -->` 引用至本 plan.md。
