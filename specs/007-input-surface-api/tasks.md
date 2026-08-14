# Tasks: Input Surface（Android MediaCodec 输入面）

**Input**: Design documents from `specs/007-input-surface-api/`

**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: 宿主 googletest 覆盖可测纯逻辑（`VideoConfig` surface 校验、格式常量）；Android 侧因输入面无法在宿主运行，验证走交叉编译门禁 + 设备示例运行（C-051：`make android-surface`）。

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

- Bazel workspace root: `codec/`（`workspace(name = "video_codec")`）
- API: `codec/src/framework/api/`
- Core 类型: `codec/src/framework/core/types.h`
- Android 后端: `codec/src/framework/backend/android/`
- 示例: `codec/src/examples/`
- 验证: `codec/scripts/verify/`、`codec/mk/`

---

## Phase 1: Setup（构建接线）

**Purpose**: surface 路径的 CPU 绘制（`ANativeWindow_*`）需要链接 libandroid；framework 只存 `ANativeWindow*` 指针（类型来自 NDK 头，无需链接），`-landroid` 仅示例需要（research R6）

- [x] T001 在 `codec/src/examples/BUILD.bazel` 为 `encode_file` 目标按平台 `select()` 增加 Android 侧 `linkopts = ["-landroid"]`（`//platforms:android_arm64_platform_setting` 分支；宿主分支保持空），使 `--surface` 模式的 `ANativeWindow_lock/setBuffersGeometry/unlockAndPost` 可链接

**Checkpoint**: Android 构建可链接 ANativeWindow 符号——surface 模式示例可实现

---

## Phase 2: Foundational（API 与配置基础）

**Purpose**: 所有用户故事共享的接口基础——`VideoConfig.input_surface` 声明 + `CreateInputSurface()` 返回 `void*` + 删除 `InputSurface`（spec FR-010；data-model VideoConfig；contract C-001~C-003、C-010/C-011）

**⚠️ CRITICAL**: US1 的设备验证依赖"示例可构建"；本阶段先完成接口变更与宿主回归门禁

- [x] T002 [P] 在 `codec/src/framework/core/types.h`：`VideoConfig` 增加 `bool input_surface = false`；扩展 `IsValid()`——`input_surface == true` 时宽高为正且偶数、`codec == kH264`，否则返回 `kInvalidArgument`（data-model validation；contract C-011）
- [x] T003 [P] 在 `codec/src/framework/api/video_encoder.h`：`CreateInputSurface()` 返回类型改为 `void*`（默认实现返回 `nullptr`），删除 `InputSurface` 前向声明；删除文件 `codec/src/framework/api/input_surface.h`（contract C-001；FR-010）
- [x] T004 新增宿主单测：在 `codec/tests/utils/mediacodec_utils_test.cc`（或新增 `codec/tests/core/` 测试）覆盖 `VideoConfig::IsValid()` 的 surface 校验分支与 `COLOR_FormatSurface` 常量（0x7F000789）；在 `codec/tests/.../BUILD.bazel` 注册并跑通 `bazel test`
- [x] T005 宿主回归门禁：`CreateInputSurface` 签名变更后执行 `bazel build //...` 与 `bazel test //...`，确认 FFmpeg 后端/示例/既有测试编译与行为无回归（contract C-052）

**Checkpoint**: API 就绪、宿主无回归——后端 surface 模式可实现

---

## Phase 3: User Story 1 - Android 硬件输入面（绘制→系统投递→编码）(Priority: P1) 🎯 MVP

**Goal**: `MediaCodecVideoEncoder` surface 模式——`VideoConfig.input_surface=true` 时以 `COLOR_FormatSurface` 配置、`createInputSurface` 创建硬件输入面、`CreateInputSurface()` 返回 `void*`（`ANativeWindow*`），调用者绘制后系统自动投递编码（spec US1；data-model MediaCodecVideoEncoder；contract C-002/C-004/C-005/C-013、C-020/C-021）

**Independent Test**: 在 Android 设备上运行 `make android-surface`：`encode_file --surface` CPU 绘制 60 帧 RGBA SMPTE 进输入面 → 产出 `clip-android.mp4`（h264+aac），ffprobe 双流 + ffmpeg 解码校验通过；宿主机同一代码返回 `nullptr`

### Implementation for User Story 1

- [x] T006 [P] [US1] 在 `codec/src/framework/backend/android/mediacodec_video.h`：新增成员 `ANativeWindow* surface_window_ = nullptr`（含 `#include <android/native_window.h>`）与 `bool SurfaceMode() const`（=`config_.input_surface`）
- [x] T007 [US1] 在 `codec/src/framework/backend/android/mediacodec_video.cc` `Init()`：`input_surface=true` 时以 `AMEDIAFORMAT_KEY_COLOR_FORMAT = COLOR_FormatSurface`(0x7F000789) 配置（不复用 `ColorFormatFor(input_format)`）；configure+start 成功后调用 `AMediaCodec_createInputSurface(codec_, &surface_window_)`，失败返回 `kPlatformUnsupported`（contract C-040）
- [x] T008 [US1] 在 `mediacodec_video.{h,cc}`：override `void* CreateInputSurface()`——surface 模式且已 `Init()` 返回 `surface_window_`（按 `void*`，幂等）；否则返回 `nullptr`（contract C-002/C-005）
- [x] T009 [US1] 在 `mediacodec_video.cc`：`Encode(VideoFrame)` 与 `Encode(NativeBuffer)` 入口处检查 `SurfaceMode()`，为真时返回 `kUnsupportedOperation`（输入模式互斥；contract C-012）
- [x] T010 [US1] 在 `codec/src/examples/encode_file.cc`：新增 `--surface` 模式（`#ifdef __ANDROID__` 守卫，非安卓构建忽略/报 unsupported）：`cfg.input_surface=true` 创建编码器 → `CreateInputSurface()` 取窗口 → CPU 绘制 RGBA SMPTE 60 帧（`ANativeWindow_setBuffersGeometry(w,h,WINDOW_FORMAT_RGBA_8888)` → `lock` 填 `buf.bits` → 时间戳（`dlsym("ANativeWindow_setBuffersTimestamp")` 解析，失败则跳过）→ `unlockAndPost`）→ `Flush()` → 输出 `clip-android.mp4`（复用既有 muxer/queue 装配）
- [x] T011 [US1] 在 `codec/scripts/verify/android_codec.sh`：新增 `surface` 模式（build → push → 设备运行 `./encode_file --surface clip 2` → pull `clip-android.mp4` → ffprobe h264+aac + ffmpeg 解码校验，复用既有 run 校验逻辑）；在 `codec/mk/android.mk` 注册 `android-surface` 目标
- [x] T012 [US1] 设备验证：`make android-surface` PASS——产出合法 H.264+AAC MP4，解码校验通过（contract C-051；SC-001/SC-002）

**Checkpoint**: US1 完整可用——MVP 达成，可独立演示

---

## Phase 4: User Story 2 - 系统背压与缓冲语义 (Priority: P2)

**Goal**: 输入面的缓冲队列与背压由系统保障（`ANativeWindow` 自带队列），框架不引入额外无界缓冲；高提交速率下内存有界（spec US2；contract C-022）

**Independent Test**: 以超过编码吞吐的速率连续绘制提交（≥3 倍时长）后内存占用有界、无 OOM；编码输出完整可解码

### Implementation for User Story 2

- [x] T013 [US2] 在 `mediacodec_video.cc` surface 路径与 `contracts/input-surface-contract.md`：确认/落实"框架不缓冲、背压透传系统"（无自有缓冲池代码路径；代码注释 + C-022 校准）
- [x] T014 [US2] 在 `encode_file.cc` `--surface` 模式支持可配置帧数/速率（如 `--surface [frames]`），并在 `android_codec.sh surface` 增加高帧率档（如 180 帧，≥3 倍编码吞吐）；设备验证内存有界、输出完整（contract C-022；SC-003）

**Checkpoint**: US1+US2 均独立可用——背压语义验证通过

---

## Phase 5: User Story 3 - 生命周期、EOS 与输入模式互斥 (Priority: P3)

**Goal**: surface 模式生命周期与编码器状态机一致（未 Init/Release 后操作拒绝）；`Flush()` 经 `signalEndOfInputStream` 触发 EOS 并带 deadline 兜底（安卓通用，研究 R3）；互斥（spec US3；contract C-030~C-032、C-041~C-043）

**Independent Test**: 在 Android 设备上验证：未 `Init()` 请求输入面 → 拒绝；surface 下 `Encode(VideoFrame)` → 明确错误；`Flush()` 后剩余帧完整输出、状态可回收

### Implementation for User Story 3

- [x] T015 [US3] 在 `mediacodec_video.cc` `Flush()`：surface 模式改为 `AMediaCodec_signalEndOfInputStream(codec_)` + drain；drain 以 EOS 缓冲到达**或 deadline 超时**（默认 5s，可配置常量）结束，不永久阻塞（contract C-031；研究 R3）
- [x] T016 [US3] 在 `mediacodec_video.cc` `Release()`：surface 模式下释放句柄引用/置空 `surface_window_`（句柄有效至 Release，之后操作拒绝；不泄漏、不 double-free）（contract C-004/C-007）
- [x] T017 [US3] 在 `mediacodec_video.cc`：`CreateInputSurface()` 生命周期守卫——编码器非 `Initialized` 状态（未 Init / 已 Release）返回 `nullptr` 且不创建（contract C-004/C-040）
- [x] T018 [US3] 设备验证：互斥（surface 下 `Encode` 报错）、`Flush()` 剩余帧完整、未 Init/Release 后操作拒绝（contract C-053；SC-004）

**Checkpoint**: US1+US2+US3 均独立可用——生命周期与 EOS 语义验证通过

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: 回归、文档与整体校验（多用户故事共享）

- [x] T019 全量回归：`bazel test //...`（宿主）+ `make host_ffmpeg_codec` + `make android-run`（CPU 帧路径）+ `make android-raw`（编码器独立）——新增 surface 路径后既有路径无回归（contract C-052；SC-005）
- [x] T020 文档与注释校准：`specs/007-input-surface-api/` 各文档（research/data-model/contract/quickstart）与实际实现一致；`quickstart.md` 最短调用可运行；删除已废弃的 `InputSurface` 引用（含 `audio_encoder.h` 注释等）

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: 无依赖，可立即开始
- **Foundational (Phase 2)**: 依赖 Setup——**阻塞所有用户故事**
- **User Stories (Phase 3+)**: 依赖 Foundational；按优先级顺序（P1→P2→P3）或并行
- **Polish (Phase 6)**: 依赖 US1~US3 完成

### User Story Dependencies

- **US1 (P1)**: 依赖 Phase 1/2；独立可测（`make android-surface`）
- **US2 (P2)**: 依赖 US1（背压验证在 surface 路径上）；独立可测（高帧率档）
- **US3 (P3)**: 依赖 US1（生命周期/EOS 在 surface 路径上）；独立可测

### Within Each User Story

- 核心实现先于验证；US1 中示例（T010）依赖后端（T006~T009）
- 每个故事完成后可独立验证（checkpoint）

### Parallel Opportunities

- T001/T002/T003（Setup + Foundational 的 API 与接线）可并行（不同文件）
- T004/T005 依赖 T002/T003
- US1 内 T006（头文件）与 T010（示例）可部分并行；T007~T009 顺序依赖 T006
- US2/US3 依赖 US1 完成

---

## Parallel Example: User Story 1

```bash
# 并行：接口/配置基础
Task: "T002 VideoConfig.input_surface + IsValid in codec/src/framework/core/types.h"
Task: "T003 CreateInputSurface() -> void* + delete input_surface.h in codec/src/framework/api/"

# 顺序：后端 surface 模式（T006 -> T007/T008/T009）
Task: "T006 mediacodec_video.h 新增 surface_window_ 成员"
Task: "T007 Init() COLOR_FormatSurface + createInputSurface in mediacodec_video.cc"
Task: "T010 encode_file --surface 模式（示例，依赖 T007）"
```

---

## Implementation Strategy

### MVP First（仅 User Story 1）

1. 完成 Phase 1（`-landroid` 接线）
2. 完成 Phase 2（API + 配置 + 宿主门禁）
3. 完成 Phase 3（后端 surface 模式 + 示例 `--surface` + 设备验证）
4. **STOP and VALIDATE**: `make android-surface` 独立验证 US1（MVP）
5. 需要时演示/交付

### Incremental Delivery

1. Setup + Foundational → API 就绪
2. US1 → 设备验证 `make android-surface`（MVP）
3. US2 → 高帧率背压验证
4. US3 → 生命周期/EOS/互斥验证
5. Polish → 全量回归

### 注意事项

- 时间戳 `dlsym("ANativeWindow_setBuffersTimestamp")` 失败时退化为无显式时间戳（研究 R1；记录限制）
- surface 模式配置（`input_surface`）须在 `Init()` 前设定（contract C-010）
- 示例 `--surface` 为 Android-only（`__ANDROID__` 守卫），非安卓构建不包含该路径

---

## Notes

- [P] 任务 = 不同文件、无依赖
- [Story] 标签映射到 spec.md 用户故事
- 每个用户故事可独立完成与测试
- 提交在每个任务或逻辑组完成后
- 在 checkpoint 停止以独立验证故事
- 避免：含糊任务、同文件冲突、破坏独立性的跨故事依赖
