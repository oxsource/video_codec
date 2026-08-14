# Tasks: Android MediaCodec 后端实现

**Input**: Design documents from `specs/006-android-mediacodec-backend/`

**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: 宿主 googletest 单测覆盖纯逻辑（`mediacodec_utils`，见 plan Testing）；Android 侧因 MediaCodec 无法在宿主运行，验证走交叉编译门禁 + 设备/模拟器示例运行（C-051）。

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

- Bazel workspace root: `codec/`（`workspace(name = "video_codec")`）
- Backend: `codec/src/framework/backend/android/`
- 示例: `codec/src/examples/`
- 测试: `codec/tests/`
- NDK 接线: `codec/WORKSPACE`、`codec/third_party/android_ndk/BUILD.bazel`

---

## Phase 1: Setup (NDK 接线与构建基础)

**Purpose**: 使 Android 交叉编译可行且宿主保持 NDK-free（research R1；spec FR-008）

- [x] T001 在 `codec/WORKSPACE` 注册 `android_ndk_repository(name = "androidndk")`（路径经 `$ANDROID_NDK_HOME` 解析；本地 NDK 25.2.9519653）
- [x] T002 重写 `codec/third_party/android_ndk/BUILD.bazel`：`android_media_codec` 目标改为链接 libmediandk（`linkopts = ["-lmediandk"]`），保留 `target_compatible_with = ["@platforms//os:android"]`
- [x] T003 验证门禁生效：`bazel test //tests/...`（宿主不拉入 NDK）且 `make android-verify`（`codec/scripts/verify/android_build.sh`）交叉编译通过

**Checkpoint**: NDK 可用、宿主构建无回归——后端/示例可开始实现

---

## Phase 2: Foundational (纯逻辑与示例改造)

**Purpose**: 所有用户故事共享的基础——可宿主单测的纯逻辑 + 可运行的 backend 无关示例（FR-011/012），二者独立可验证

**⚠️ CRITICAL**: 示例改造是 US1 设备验证的前置（US1 Independent Test 依赖"运行框架示例"）

- [x] T004 [P] 实现 `codec/src/framework/backend/android/mediacodec_utils.{h,cc}`：color-format/mime 映射、输入缓冲尺寸计算、Annex-B 组装（关键帧前置 SPS/PPS）、csd 提取（`BUFFER_FLAG_CODEC_CONFIG` → sps/pps/asc）、AAC ASC 字节打包（纯函数，无 AMediaCodec 依赖）
- [x] T005 新增宿主单测 `codec/tests/utils/mediacodec_utils_test.cc`（覆盖 T004 各函数：I420→19/NV12→21、mime 映射、Annex-B 起始码、csd 提取、非法输入返回空），并在 `codec/tests/utils/BUILD.bazel` 注册 `mediacodec_utils_test` 目标（依赖 T004）
- [x] T006 示例改造：`codec/src/examples/ffmpeg_encode_file.cc` 改名 `encode_file.cc`；新增 `--backend auto|android|ffmpeg` 参数（缺省 `auto`，用 `CodecFactory::ResolveBackend` 得到生效 backend）；输出文件名追加 `-<backend>` 后缀（`BackendToString` 规范名，如 `out-ffmpeg.mp4`、`out-android.mp4`，raw 模式 `out-ffmpeg.h264`）
- [x] T007 示例构建平台条件化：`codec/src/examples/BUILD.bazel` 目标改名 `encode_file`，FFmpeg 归档 `data`/`-Wl,-force_load`/`linkstatic` 改为仅宿主（`select()` 按 `//platforms:android_arm64_platform_setting` 分离），使同一目标可在 Android 交叉编译
- [x] T008 宿主验证示例：`bazel run //src/examples:encode_file -- out 2` 产出 `out-ffmpeg.mp4`（双轨）；`--raw` 回归产出 `out-ffmpeg.h264`；`--backend auto` 在宿主解析为 FFmpeg

**Checkpoint**: 纯逻辑有宿主单测保障；示例 backend 无关、宿主可跑——US1/2/3 可并行开始

---

## Phase 3: User Story 1 - Android 统一 API 视频硬件编码 (Priority: P1) 🎯 MVP

**Goal**: `MediaCodecVideoEncoder` 实现 `VideoEncoder` 抽象（H.264/HEVC、CPU 输入路径），经注册后 `Backend::kAuto` 的 Android 构建自动使用系统硬件编码（spec US1；data-model MediaCodecVideoEncoder；contract C-010~C-015）

**Independent Test**: 在 Android 设备/模拟器上运行 `encode_file clip 2`（`--backend auto`）→ 产出 `clip-mediacodec.mp4`，含可播放 H.264 视频轨

### Implementation for User Story 1

- [x] T009 [US1] 实现 `codec/src/framework/backend/android/mediacodec_video.{h,cc}`：`MediaCodecVideoEncoder`（`createEncoderByType("video/avc"/"video/hevc")` + AMediaFormat 配置 + dequeue/get/queue 循环；`Encode(VideoFrame)` CPU 拷贝；`Encode(NativeBuffer)`/`CreateInputSurface` 返回 unsupported/空；`EncoderLifecycle`；push/pull 双模式；`Flush`/`Release`；Annex-B 输出经 `mediacodec_utils`）
- [x] T010 [US1] 新增 `codec/src/framework/backend/android/register.cc` 注册视频 creator（`RegisterVideo(Backend::kAndroid, ...)`），并在 `backend/android/BUILD.bazel` 增加 `register` 目标（`alwayslink = True`，deps: `:mediacodec_video`、`:mediacodec_utils`、api:codec_factory）
- [x] T011 [US1] Android 验证：`bazel build //src/examples:encode_file --config android_arm64` 交叉编译通过（输出 ELF aarch64 + `NEEDED libmediandk.so`，`RegisterAndroid` 注册符号已链接）；设备运行（`adb` 无已连接设备，需硬件/模拟器，见 Completion Report 附注）

**Checkpoint**: US1 独立可用——Android 上通过统一 API 完成硬件视频编码（MVP）

---

## Phase 4: User Story 2 - AAC 音频编码与声像合成 (Priority: P2)

**Goal**: `MediaCodecAudioEncoder` 实现 `AudioEncoder` 抽象（AAC），音频/视频共用同一 queue，与桌面一致的 push 流程（spec US2；data-model MediaCodecAudioEncoder；contract C-020~C-023）

**Independent Test**: 音频编码器 push 模式在 Android 上产出有序 AAC 包（经 queue 送达）；完整"声像合成→封装"端到端验证依赖 US3（交叉依赖，记录见下）

### Implementation for User Story 2

- [x] T012 [P] [US2] 实现 `codec/src/framework/backend/android/mediacodec_audio.{h,cc}`：`MediaCodecAudioEncoder`（`createEncoderByType("audio/mp4a-latm")` + S16 交错 PCM 输入拷贝；AAC 输出；`BUFFER_FLAG_CODEC_CONFIG` → ASC 提取；`EncoderLifecycle`；push/pull 双模式；`Flush`/`Release`）
- [x] T013 [US2] 在 `codec/src/framework/backend/android/register.cc` 追加音频 creator（`RegisterAudio(Backend::kAndroid, ...)`），`BUILD.bazel` 的 `register` 目标补 deps `:mediacodec_audio`
- [x] T014 [US2] 音频路径验证：`bazel build //src/examples:encode_file --config android_arm64` 交叉编译通过（mediacodec_audio 编译 + RegisterAndroid 音频注册已链接）；宿主回归 16/16；设备端 A/V 端到端需硬件（随 US3，见 Completion Report 附注）

**Checkpoint**: US1 + US2 各自独立工作；完整声像合成随 US3 端到端验证

---

## Phase 5: User Story 3 - Android 原生封装（MediaMuxer）(Priority: P2)

**Goal**: `MediaCodecMuxer` 实现 `Muxer` 抽象，经 queue→Await 直接衔接，产出合法 MP4（spec US3；data-model MediaCodecMuxer；contract C-030~C-035；research R4 临时文件承载 + Finish 回放）

**Independent Test**: 在 Android 上运行 `encode_file clip 2` → `clip-mediacodec.mp4` 含 H.264 + AAC 双轨，`ffprobe` 结构合法

### Implementation for User Story 3

- [x] T015 [P] [US3] 实现 `codec/src/framework/backend/android/mediacodec_muxer.{h,cc}`：`MediaCodecMuxer`（`AMediaMuxer_new(tmpfile fd, MP4)`；csd 捕获 → `addTrack` 前写入 AMediaFormat `csd-0/1`；懒打开（首关键帧/首 csd）；`writeSampleData`；`Finish()` 时 `AMediaMuxer_stop` → rewind → 一次性回放全部字节到 `ByteSink::Write` + `Flush`；`Finish`/`Release` 幂等）
- [x] T016 [US3] 在 `codec/src/framework/backend/android/register.cc` 追加 muxer creator（`RegisterMuxer(Backend::kAndroid, ...)`），`BUILD.bazel` 的 `register` 目标补 deps `:mediacodec_muxer`
- [x] T017 [US3] 设备端到端验证：`bazel build //src/examples:encode_file --config android_arm64` 交叉编译通过（`MediaCodecMuxer` 注册符号已链接，宿主回归 16/16）；`encode_file clip 2` → `clip-mediacodec.mp4` 双轨实机运行需 Android 设备/模拟器（`adb` 无已连接设备，见 Completion Report 附注）

**Checkpoint**: 全部用户故事独立可用——Android 声像合成全链路闭环

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: 全功能收尾、回归与文档一致

- [x] T018 [P] 全量宿主回归：`bazel test //tests/...`（既有测试 + 新增 `mediacodec_utils_test`）全部通过，FFmpeg 后端行为不变（FR-010/SC-004）——另 `make host-verify`（build //... + spike + encode_file 示例 + ffprobe 断言）通过
- [x] T019 [P] 文档核对：`specs/006-android-mediacodec-backend/quickstart.md` 命令与实际一致（`encode_file`、`-<backend>` 文件名、`--config android_arm64` 显式构建、16 项测试）；`CODEBUDDY.md` SPECKIT 引用核对通过
- [x] T020 验收走查：spec FR-001~FR-012 与 `contracts/android-backend-contract.md` C-001~C-052 逐条映射（见 Completion Report 覆盖表）；边界用例实测——宿主 `--backend android` 报错、未知 backend/missing value 报错、宿主显式 `--backend ffmpeg` 正常；HEVC 设备语义由构造保证（createEncoderByType 返回空 → kPlatformUnsupported），实机项待硬件

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: 无依赖，可立即开始；**BLOCKS** 所有后续（NDK 未接线则 Android 无法编译）
- **Foundational (Phase 2)**: 依赖 Setup；其中 T004/T005（mediacodec_utils）与 T006/T007/T008（示例）相互独立，可并行
- **US1 (Phase 3)**: 依赖 Foundational（示例改造）——US1 设备验证用示例
- **US2 (Phase 4)**: 依赖 Foundational；与 US1/US3 并行开发（音频编码器独立文件）
- **US3 (Phase 5)**: 依赖 Foundational；与 US1/US2 并行开发（muxer 独立文件）
- **Polish (Phase 6)**: 依赖全部用户故事

### User Story Dependencies

- **US1 (P1)**: Foundational 后即可开始；无其他故事依赖（MVP）
- **US2 (P2)**: Foundational 后即可开始；**独立测试可先行**（音频编码器单测/包产出验证）；"声像合成→封装"端到端验证依赖 US3（cross-dependency，记录于 US2 Independent Test）
- **US3 (P2)**: Foundational 后即可开始；端到端 A/V 验证依赖 US1+US2 产物

### Within Each User Story

- 纯逻辑 → 宿主单测（T004→T005）
- 编码器/封装器实现 → 注册（T009→T010、T012→T013、T015→T016）
- 实现 → 设备验证（T011、T014、T017）
- 故事完成后再进入下一优先级

### Parallel Opportunities

- T004 与 T006 可并行（[P]；不同文件）；T005 依赖 T004（先行）
- T006/T007 与 T004/T005 可并行（不同文件）
- US1/US2/US3 三个故事互不阻塞，可并行开发（`mediacodec_video` / `mediacodec_audio` / `mediacodec_muxer` 不同文件）
- 同一故事的实现与验证串行（验证依赖实现）

---

## Parallel Example: User Story 1

```bash
# 实现（并行于 US2/US3）：
Task: "实现 mediacodec_video.{h,cc}（US1）"
# 之后：
Task: "register.cc 注册视频 creator（US1）"
Task: "Android 交叉编译 + 设备运行验证（US1）"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Phase 1: Setup（NDK 接线）
2. Phase 2: Foundational（mediacodec_utils + 示例改造）
3. Phase 3: US1 视频编码 → 设备验证 `clip-mediacodec.mp4`（H.264）
4. **STOP and VALIDATE**: US1 独立可用即 MVP

### Incremental Delivery

1. Setup + Foundational → 基础设施就绪（宿主无回归）
2. US1 视频编码 → 设备验证 → MVP
3. US2 音频编码（可并行）→ 包产出验证
4. US3 封装（可并行）→ 声像合成端到端 → 完整交付
5. Polish → 回归 + 文档 + 验收走查

### Parallel Team Strategy

1. 团队先完成 Setup + Foundational
2. Foundational 后：开发者 A=US1、B=US2、C=US3（三个故事文件互不冲突）
3. US3 完成后联调端到端 A/V，Polish 收尾

---

## Notes

- [P] 任务 = 不同文件、无未完成依赖
- [Story] 标签映射到 spec 用户故事（US1/US2/US3），便于追溯
- 每个故事独立可完成、可测试（US2 完整端到端依赖 US3，已记录）
- 宿主无法运行 MediaCodec——设备验证为本功能验收方式（C-051）；宿主仅承担单测与回归
- 提交粒度：每个任务或逻辑组后提交（可选钩子 `/speckit.git.commit`）
- 避免：含糊任务、同文件冲突、破坏故事独立性的跨故事依赖
