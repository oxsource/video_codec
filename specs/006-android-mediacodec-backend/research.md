# Research: Android MediaCodec 后端实现

**Branch**: `006-android-mediacodec-backend` | **Date**: 2026-08-14 | **Spec**: [spec.md](spec.md)

> 澄清结论（spec）：v1 = H.264/HEVC 视频编码（CPU 输入路径）+ AAC 音频编码 + Android 原生封装（MediaMuxer 系）；Surface 零拷贝路径与 Opus 不在 v1。

## R1: NDK 仓库注册与 `//third_party/android_ndk` 接线

- **Decision**: 采用 `rules_android_ndk`（bazelbuild/rules_android_ndk v0.1.2，http_archive 固定 sha256）提供 Android cc_toolchain；在 `codec/WORKSPACE` 调用其 `android_ndk_repository(name = "androidndk")`（路径经 `ANDROID_NDK_HOME` 解析，缺省 api_level 31）。工具链注册**不**放在 WORKSPACE 的 `register_toolchains`（会迫使宿主也拉取/校验 NDK 仓库），而是放入 `.bazelrc` 的 `build:android_arm64 --extra_toolchains=@androidndk//:all`，宿主（非 Android）构建因此永不拉取 `@androidndk`（FR-008，宿主测试在无 `ANDROID_NDK_HOME` 下通过）。`//third_party/android_ndk:android_media_codec` 目标改为 `linkopts = ["-lmediandk"]`（NDK sysroot 的 `usr/lib/<triple>/<api>/libmediandk.so`），保留 `target_compatible_with = ["@platforms//os:android"]`。
- **关键适配**: Bazel 6 的 `--incompatible_enable_cc_toolchain_resolution` 默认 **false**——cc 工具链走旧式 `--cpu` 路径，`--platforms` 不驱动 cc 工具链选择（实测会回退到宿主工具链，`mediacodec_spike` 以 macOS clang 编译）。必须在 `build:android_arm64` 显式 `--incompatible_enable_cc_toolchain_resolution=true`，`--platforms` + `--extra_toolchains` 才生效并选中 NDK clang（`--target=aarch64-linux-android31`，编译链接均通过，输出 ELF aarch64 + `NEEDED libmediandk.so`）。`make android-verify`（`android_build.sh`）改用 `--config android_arm64` 统一携带这些选项。
- **Rationale**: Bazel 6.5 已从 `@bazel_tools` 移除内置 `android_ndk_repository`（`tools/android/android_repository.bzl` 不存在），rules_android_ndk 是维护中的标准替代；NDK r25（sysroot 布局，无顶层 `platforms/` 目录）被其正确识别。
- **Alternatives**: 内置 `android_ndk_repository`（Bazel 6.5 已移除，不可用）；`register_toolchains("@androidndk//:all")` 放 WORKSPACE（实测导致宿主在无 NDK 环境下拉取 `@androidndk` 并失败，违背 FR-008，弃用）。
- **Local env**: `ANDROID_NDK_HOME=/Users/moks/Library/Android/sdk/ndk/25.2.9519653`（NDK 25.2.9519653）；CI 需等价提供。

## R2: MediaCodec 视频编码（CPU 路径）

- **Decision**: 采用 NDK `AMediaCodec` 标准 "dequeue/get/queue" 循环，输出经 `dequeueOutputBuffer`/`getOutputBuffer` 组装为 Annex-B（关键帧前置 SPS/PPS），`BUFFER_FLAG_KEY_FRAME` → `keyframe`。
- **Rationale**: 与既有 `codec/src/spike/mediacodec_spike.cc`（已验证 `video/avc` 循环）一致；输出格式与桌面一致（Annex-B），供下游队列/封装直接消费。
- **API 形态**（NDK 25.2.9519653 头文件确认）:
  - `AMediaCodec_createEncoderByType("video/avc" | "video/hevc")`
  - 配置 `AMediaFormat`：`MIME`/`WIDTH`/`HEIGHT`/`COLOR_FORMAT`/`BIT_RATE`/`FRAME_RATE`/`I_FRAME_INTERVAL`；`COLOR_FORMAT` 依 `VideoConfig.input_format` 映射（I420 → `COLOR_FormatYUV420Planar`(19)，NV12 → `COLOR_FormatYUV420SemiPlanar`(21)）
  - 输入：`dequeueInputBuffer` → `getInputBuffer` → 按平面拷贝 `VideoFrame` → `queueInputBuffer(index, offset, size, pts, 0)`
  - 输出：`dequeueOutputBuffer` → `getOutputBuffer`；`BUFFER_FLAG_CODEC_CONFIG` 为 SPS/PPS（关键帧组装时前置）；`BUFFER_FLAG_KEY_FRAME` → `keyframe`；`releaseOutputBuffer(index, false)`
  - `AMediaCodec_flush`（Flush 时冲刷剩余输出）、`stop`/`delete`（Release）
- **Alternatives**: Surface 零拷贝输入路径（v1 排除，spec 澄清 Q2:B；`CreateInputSurface()` 保持返回空）。

## R3: MediaCodec 音频编码（AAC）

- **Decision**: `createEncoderByType("audio/mp4a-latm")`，输入直接拷贝 `AudioFrame`（S16 交错 PCM），输出裸 AAC 帧；`BUFFER_FLAG_CODEC_CONFIG` 缓冲 → AudioSpecificConfig（供封装 track 的 `csd-0`）。
- **Rationale**: 与视频编码对称；输出（裸 AAC 帧）与桌面 FFmpeg 音频编码一致，可经同一封装后端封装。
- **API 形态**: 配置 `MIME`/`SAMPLE_RATE`/`CHANNEL_COUNT`/`BIT_RATE`/`MAX_INPUT_SIZE`；输入 `dequeueInputBuffer`/`getInputBuffer`/`queueInputBuffer`，输出 `dequeueOutputBuffer`/`getOutputBuffer`。
- **Alternatives**: Opus（v1 排除，spec 澄清 Q1:A）。

## R4: AMediaMuxer 封装后端与 ByteSink 契约

- **Decision**: Android 封装后端用**可 seek 临时文件**承载 `AMediaMuxer` 输出，`Finish()` 时整体回放给 `io::ByteSink`：
  1. `tmpfile()`（可 seek、跨平台）取 fd；`AMediaMuxer_new(fd, AMEDIA_MUXER_OUTPUT_FORMAT_MPEG_4)`
  2. `Push` 期间：捕获各轨首个 `BUFFER_FLAG_CODEC_CONFIG` 缓冲 → 写入 `AMediaFormat` 的 `csd-0`/`csd-1`；`AMediaMuxer_addTrack` → `start` → `writeSampleData(trackIdx, data, {offset,size,pts,flags})`
  3. `Finish()`：`AMediaMuxer_stop()` → rewind → 读出全部字节 → `ByteSink::Write` 一次性投递 → `Flush`
- **Rationale**: `AMediaMuxer_new(int fd, format)` 只接受 fd（NDK 头文件确认，API 21+），且需要**可 seek** 的 fd（moov 在 `stop()` 时回写）。临时文件天然可 seek，规避"ByteSink 未必可 seek"问题；字节在 Finish 一次投递，ByteSink 的 seek 能力无关紧要，`Muxer`/`ByteSink` 契约保持成立。
- **Alternatives**:
  - pipe + 后台线程转发 → 否决：pipe 不可 seek，`AMediaMuxer` `stop()` 回写 moov 时失败
  - 要求 ByteSink 为 `FileByteSink` 并传递其文件 fd → 否决：耦合具体 sink 实现，破坏通用契约
- **行为差异**（写入 contract）：Android 封装为"停止时一次性产出"（非分片）；FFmpeg 封装 v1 为分片增量。两者输出均为合法 MP4，媒体内容一致。

## R5: 注册与构建接线

- **Decision**: `backend/android/register.cc`（`alwayslink = True`）静态注册 `CodecFactory::RegisterVideo/Audio/Muxer(Backend::kAndroid, ...)`；`public` 已通过 `select()` 在 `//platforms:android_arm64_platform_setting` 下链接 `backend/android:android`。
- **Rationale**: 与 FFmpeg backend 完全同构（`register.cc` + `alwayslink` 保证静态初始化被链接）；`ResolveBackend` 已确认 `__ANDROID__ → kAndroid`（spec FR-006/FR-007）。
- **依赖约束**: 后端仅依赖自己的外部依赖（libmediandk），不引入 FFmpeg（架构不变式，spec Assumptions）。
- **Alternatives**: 集中式 `#ifdef` 分发（ADR-002 已否决——新增后端应为一个子目录 + select 条目）。

## R6: 测试策略

- **Decision**: 纯逻辑（格式键构造、颜色格式映射、缓冲尺寸计算、Annex-B 组装、ASC/csd 提取等）抽为可宿主单测的 `mediacodec_utils`；Android 集成验证走"交叉编译门禁 + 设备/模拟器示例运行"（沿用 `codec/mk/android.mk` → `android-verify` / `mediacodec_spike` 模式）。
- **Rationale**: MediaCodec/MediaMuxer 无法在宿主运行；repo 尚无 android_instrumentation_test 基建。交叉编译门禁低成本捕获 NDK 破坏（spec 002 R2），宿主单测覆盖可测逻辑。
- **Alternatives**: `android_instrumentation_test`（需模拟器/设备 CI 基建，v1 不做，记录为后续项）。
