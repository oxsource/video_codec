# Tasks: Muxer 与 Encoder 分层设计

**Input**: Design documents from `/specs/005-muxer-encoder-layering/`

**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: 项目为库，测试任务按计划生成（contract + 集成）。测试先写并预期失败（TDD）。

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

- 仓库根: `codec/`（Bazel workspace 在 `codec/`，源码在 `codec/src/framework/`，测试在 `codec/tests/`）

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: 基线确认与纯重构（不影响任何用户故事，先保证现有测试绿）

- [x] T001 确认工作区在 feature 分支 `005-muxer-encoder-layering`，`bazel test //tests/...` 基线全绿（13 测试）
- [x] T002 [P] 阅读 `codec/src/framework/queue/queue_iface.h`、`codec/src/framework/consumer/packet_consumer.h`，确认 PacketSink 仅依赖 core 类型（无 queue 特有依赖）

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: PacketSink 迁入 core + api 层 Muxer 接口与配置 —— 所有用户故事的共同基础

**⚠️ CRITICAL**: 本阶段完成前不能开始任何用户故事实现

- [x] T003 新建 `codec/src/framework/core/packet_sink.h`：将 `PacketSink`（Push(VideoPacket&&)/Push(AudioPacket&&)/Flush/Finish）从 `queue/queue_iface.h` 原样迁入（include `core/status.h` + `core/types.h`），更新注释说明其跨层契约（生产端 encoder 与消费端 muxer/consumer 共用）
- [x] T004 修改 `codec/src/framework/queue/queue_iface.h`：移除 PacketSink 定义，`#include "core/packet_sink.h"`，保留 `Backpressure`/`PacketSource`
- [x] T005 修改 `codec/src/framework/consumer/packet_consumer.h`：include 从 `queue/queue_iface.h` 改为 `core/packet_sink.h`（保持 `PacketConsumer : public PacketSink`）
- [x] T006 [P] 更新 `codec/src/framework/core/BUILD.bazel`：新增 `packet_sink` target（hdrs=["packet_sink.h"]），聚合 `core` target 加入依赖；更新 `queue/BUILD.bazel`、`consumer/BUILD.bazel` 中相关 target 依赖（如需要）
- [x] T007 运行 `bazel test //tests/...`，确认 PacketSink 迁移后 13 测试全绿（纯重构，无行为变化）
- [x] T008 修改 `codec/src/framework/core/types.h`：新增 `enum class MuxFormat { kMp4 };` 与 `struct MuxerConfig { MuxFormat format=kMp4; bool fragmented=true; int width=0; int height=0; int fps=30; Backend backend=kAuto; bool IsValid() const { return width>0 && height>0; } };`（`data-model.md` §MuxerConfig）
- [x] T009 新建 `codec/src/framework/api/muxer.h`：通用 `Muxer : public PacketSink` 抽象（`static Create(const MuxerConfig&)`、`virtual Status SetOutput(ByteSink*)`、`Push(VideoPacket&&) override=0`、`Push(AudioPacket&&) override` 默认 `kUnsupportedOperation`、`Flush()/Finish() override=0`、`virtual void Release()=0`），`class ByteSink;` 前向声明（`contracts/muxer-contract.md` §1）
- [x] T010 更新 `codec/src/framework/api/encoder_factory.h`：新增 `using MuxerCreator = std::function<std::unique_ptr<Muxer>(const MuxerConfig&)>;`、`void RegisterMuxer(Backend, MuxerCreator);`、`std::unique_ptr<Muxer> CreateMuxer(const MuxerConfig&);`（`contracts/muxer-contract.md` §2）
- [x] T011 更新 `codec/src/framework/api/encoder_factory.cc`：Registry 增 `std::unordered_map<Backend, MuxerCreator> mux;`，实现 `RegisterMuxer`/`CreateMuxer`（经 `ResolveBackend` 选择，缺失返回 nullptr），并实现 `Muxer::Create` 转发
- [x] T012 更新 `codec/src/framework/api/BUILD.bazel`：新增 `muxer` target（deps: core:types/core:status/core:packet_sink），`encoder_factory` target 增加对 `muxer` 的依赖（若 split），聚合 `api` target 纳入
- [x] T013 运行 `bazel build //...`，确认 api/core 新 target 编译通过

**Checkpoint**: 基础就绪——PacketSink 在 core、Muxer 接口 + 配置 + 工厂已定义，可开始 US1

---

## Phase 3: User Story 1 - 通过统一 Muxer 层封装编码输出 (Priority: P1) 🎯 MVP

**Goal**: FFmpeg backend 实现 `Muxer`，删除旧 mux/Mp4Consumer，example 走 encoder→queue→muxer→sink 新路径；产出合法 MP4

**Independent Test**: `bazel run //src/examples:ffmpeg_encode_file --` 生成 `out.mp4`，`ffprobe` 可识别（mov,mp4 容器）；`bazel test //tests/...` 全绿

### Tests for User Story 1 ⚠️（先写并预期 FAIL）

- [x] T014 [P] [US1] 新建 `codec/tests/api/muxer_contract_test.cc`：StubMuxer（实现 Muxer 的最小假类）验证契约——`Push(AudioPacket&&)` 默认返回 `kUnsupportedOperation`、无 `SetOutput` 即 `Push` 返回 `kInvalidArgument`、`Finish`/`Release` 幂等（`contracts/muxer-contract.md` §6 A4/A5）
- [x] T015 [P] [US1] 新建 `codec/tests/backend/ffmpeg/muxer_test.cc`：真实编码（SMPTE 帧）→ `encoder->SetOutputSink(&q)` → `q.Await(*muxer)` → 内存 ByteSink，断言首字节 `ftyp`、Finish 后含 moov/mdat、非关键帧丢弃（`contracts/muxer-contract.md` §6 A1/A2/A3）
- [x] T016 更新 `codec/tests/api/BUILD.bazel` 与 `codec/tests/backend/ffmpeg/BUILD.bazel`：注册 `muxer_contract_test`、`muxer_test`（后者复用 encode_push_test 的 `data=["@ffmpeg//:ffmpeg_codec_archive"]` + `linkopts=["-Wl,-force_load,$(execpath @ffmpeg//:ffmpeg_codec_archive)"]` + `linkstatic=True`）
- [x] T017 运行两个新测试，确认 FAIL（编译通过但断言失败或 target 缺失）——TDD 红灯

### Implementation for User Story 1

- [x] T018 [US1] 新建 `codec/src/framework/backend/ffmpeg/ffmpeg_muxer.h`：`class FFmpegMuxer : public Muxer`，成员（`MuxerConfig`、`ByteSink* sink_`、AVFormatContext 裸指针 + RAII、stream_index、opened 标志），声明私有辅助（`OpenMuxer`、`BuildExtradata`、`AnnexBToAvcc`、`EmitPending`）（`research.md` R2/R3/R6）
- [x] T019 [US1] 新建 `codec/src/framework/backend/ffmpeg/ffmpeg_muxer.cc`：移植 `codec/src/framework/mux/mp4_muxer.cc` 的 NAL 解析/avcC 构造/AVIO 回调逻辑；实现 `SetOutput`、`Push(VideoPacket&&)`（首关键帧懒打开，产出"头+首分片"单次提交）、`Flush`、`Finish`（写尾部 + sink flush）、`Release`；音频 Push 继承默认 `kUnsupportedOperation`；`SetOutput` 前置校验返回 `kInvalidArgument`（`contracts/muxer-contract.md` §4）
- [x] T020 [US1] 更新 `codec/src/framework/backend/ffmpeg/register.cc`：静态初始化中调用 `RegisterMuxer(Backend::kFFmpeg, [](const MuxerConfig& c){ return std::make_unique<FFmpegMuxer>(c); })`
- [x] T021 [US1] 更新 `codec/src/framework/backend/ffmpeg/BUILD.bazel`：新增 `muxer` target（srcs/hdrs=ffmpeg_muxer.{h,cc}，deps: api:muxer、core:types、io:byte_sink、@ffmpeg//:ffmpeg_codec、@ffmpeg//:ffmpeg_codec_impl），`register` target 增加对 `:muxer` 的依赖，聚合 `ffmpeg` 纳入
- [x] T022 [US1] 删除 `codec/src/framework/mux/` 整目录（mp4_muxer.{h,cc}、BUILD.bazel）——逻辑已迁入 backend（`research.md` R6）
- [x] T023 [US1] 删除 `codec/src/framework/consumer/mp4_consumer.{h,cc}`；更新 `codec/src/framework/consumer/BUILD.bazel` 移除 `mp4_consumer` target 与 `//src/framework/mux:mp4_muxer` 依赖（FR-009）
- [x] T024 [US1] 更新 `codec/src/examples/ffmpeg_encode_file.cc`：默认路径改为 `CreateMuxer(mux_cfg)` → `muxer->SetOutput(file_byte_sink)` → `queue.Await(*muxer)`；`--raw` 分支保留 `FileSinkConsumer`；删除 Mp4Consumer/FileByteSink 组合构造（`quickstart.md` 走法）
- [x] T025 [US1] 更新 `codec/src/examples/BUILD.bazel`：deps 调整（去掉对已删 target 的引用，确认保留 `//src/framework/consumer`、`//src/framework/api`、`//src/framework/io`）
- [x] T026 运行 `bazel test //tests/...`：T014/T015 由红转绿，其余 13 测试保持绿（TDD 绿灯）

**Checkpoint**: US1 完成——通用 Muxer 接口 + FFmpeg 实现可用，旧 mux/Mp4Consumer 已删，example 新路径产出合法 MP4

---

## Phase 4: User Story 2 - 不同平台后端提供各自的封装实现 (Priority: P2)

**Goal**: 验证 muxer 后端注册/选择机制支持多平台；v1 仅 FFmpeg 注册，但接口与工厂对后续后端开放

**Independent Test**: `CreateMuxer` 在仅 FFmpeg 注册时返回 FFmpegMuxer；请求未注册后端返回 nullptr；contract 测试 A5 覆盖"无可用后端"路径

### Implementation for User Story 2

- [x] T027 [P] [US2] 更新 `codec/tests/api/muxer_contract_test.cc`：新增用例——`RegisterMuxer` 后 `CreateMuxer(cfg)` 返回对应实例；未注册 Backend（如 kDarwin）返回 nullptr；`cfg.backend=kAuto` 解析到当前平台（`contracts/muxer-contract.md` §2）
- [x] T028 [US2] 验证 `codec/src/framework/backend/ffmpeg/register.cc` 的 `RegisterMuxer` 调用位于 `alwayslink` 的 register target 中（链接后自注册生效，`CreateMuxer` 非空）
- [x] T029 [US2] 运行 `bazel test //tests/api:muxer_contract_test` 确认后端选择契约通过

**Checkpoint**: US2 完成——muxer 后端注册/选择机制验证通过，为后续 Android/Apple 后端预留

---

## Phase 5: User Story 3 - 编码器与封装层解耦 (Priority: P3)

**Goal**: 验证编码器协议无关——`VideoEncoderConfig` 不引用 `MuxerConfig`，编码器代码不含容器逻辑；同一编码器裸流/封装两种走法均成立

**Independent Test**: example `--raw`（裸流，FileSinkConsumer）与默认（封装，Muxer）两分支共用同一 `encoder` 配置路径；`encode_push_test` 等既有测试证明裸流模式不变

### Implementation for User Story 3

- [x] T030 [US3] 审查 `codec/src/framework/backend/ffmpeg/video_encoder.{h,cc}` 确认无任何容器/muxer 逻辑（bsf 输出仍为 Annex-B），`VideoEncoderConfig` 无容器字段（FR-005）
- [x] T031 [US3] 更新 `codec/src/examples/ffmpeg_encode_file.cc`：显式展示两分支共享 encoder（裸流分支与封装分支仅接线不同，encoder 配置一致），补充注释说明解耦
- [x] T032 [US3] 运行 `bazel test //tests/...` 全绿，确认裸流路径（encode_push_test、example --raw）不受 US1 改动影响

**Checkpoint**: US3 完成——编码器与封装层职责边界验证，两分支独立成立

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: 文档同步、example 端到端验证、质量收尾

- [x] T033 [P] 更新 `codec/doc/architecture/module-dependencies.md`：依赖图移除 `mux` 线，`core` 增 `packet_sink`，新增 `backend/ffmpeg → {api:muxer, io:byte_sink, @ffmpeg}` 边，注明 mux→@ffmpeg 违规已消除（`research.md` R6）
- [x] T034 [P] 更新 `codec/doc/project_bootstrap.md`：移除/改写 mux 模块描述（L212-213 区域），consumer 描述移除 Mp4Consumer
- [x] T035 [P] 更新 `codec/doc/architecture/output-queue.md`：consumer 列表移除 Mp4Consumer，改为"muxer 实现 PacketSink 经 Await 直接衔接"
- [x] T036 更新既有 specs 中 mux 相关引用（`specs/002/contracts/output-queue-contract.md`、`specs/002/research.md`、`specs/004/` 若提及 mux）为"Muxer 接口 + backend 实现"表述
- [x] T037 运行 `bazel run //src/examples:ffmpeg_encode_file --` 端到端：生成 `out.mp4` 且 `ffprobe -v error -show_entries format=format_name` 返回 mov/mp4；`--raw` 生成裸流
- [x] T038 全量 `bazel test //tests/...` 确认 15+ 测试全绿（原 13 + 新增 2）；`clang-format` 检查新增/修改文件
- [x] T039 全仓 grep 确认 `Mp4Muxer`/`Mp4Consumer`/`mp4_muxer`/`mp4_consumer`/`mux/` 无残留引用

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: 无依赖，可立即开始
- **Foundational (Phase 2)**: 依赖 Setup；**阻塞所有用户故事**（PacketSink 迁 core 是 Muxer 接口前提）
- **US1 (Phase 3)**: 依赖 Foundational——核心实现
- **US2 (Phase 4)**: 依赖 US1（注册/选择机制基于 Muxer 接口与工厂）
- **US3 (Phase 5)**: 依赖 US1（example 新接线）；验证性为主
- **Polish (Phase 6)**: 依赖所有用户故事完成

### User Story Dependencies

- **US1 (P1)**: Foundational 后可开始；无其他故事依赖
- **US2 (P2)**: 依赖 US1 的接口/工厂/注册；可并行验证（T027 与 T018-T025 不同文件）
- **US3 (P3)**: 依赖 US1 的 example 走法；验证性

### Within Each User Story

- 测试先写并 FAIL（TDD），再实现
- 接口/契约 → 后端实现 → 接线 → 集成验证

### Parallel Opportunities

- Phase 2 中 T003-T006 可并行（不同文件：core/packet_sink.h、queue_iface.h、packet_consumer.h、BUILD）
- US1 测试 T014/T015 可并行（不同测试文件）
- Phase 6 文档任务 T033/T034/T035 可并行（不同文档）
- US1 实现（T018-T025）与 US2 验证（T027）部分可并行，但建议 US1 完成后开始 US2

---

## Parallel Example: Foundational 与 US1 测试

```bash
# Foundational 并行（不同文件）：
Task: "新建 core/packet_sink.h（T003）"
Task: "修改 queue/queue_iface.h（T004）"
Task: "修改 consumer/packet_consumer.h（T005）"
Task: "更新 core/BUILD.bazel（T006）"

# US1 测试并行（不同测试文件）：
Task: "新建 api/muxer_contract_test.cc（T014）"
Task: "新建 backend/ffmpeg/muxer_test.cc（T015）"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Phase 1 Setup → 基线确认
2. Phase 2 Foundational → PacketSink 迁 core + Muxer 接口/配置/工厂
3. Phase 3 US1 → FFmpegMuxer + 删旧 mux/Mp4Consumer + example 新路径
4. **STOP and VALIDATE**: example 产出合法 MP4（ffprobe），全量测试绿
5. US2/US3 为验证性增量

### Incremental Delivery

1. Setup + Foundational → 接口就绪（muxer_contract_test 红灯）
2. US1 → FFmpegMuxer + 集成测试转绿 → example 可跑（MVP）
3. US2 → 后端注册/选择契约验证
4. US3 → 解耦边界验证
5. Polish → 文档/端到端/残留清理

---

## Notes

- [P] tasks = different files, no dependencies
- T003（PacketSink 迁移）是纯重构，必须先保证 13 测试绿再继续
- FFmpegMuxer 移植自 mp4_muxer.cc，注意保留已修复的内存正确性（BuildExtradata 的 nalen<4 防护、OOM 检查、Drain 相关 unref）
- example 的 `--raw` 分支与默认分支共用 encoder 配置，体现 US3 解耦
- Commit after each task or logical group
