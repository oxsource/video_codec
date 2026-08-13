# Implementation Plan: Muxer 与 Encoder 分层设计

**Branch**: `005-muxer-encoder-layering` | **Date**: 2026-08-13 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/005-muxer-encoder-layering/spec.md`

## Summary

引入通用 `Muxer` 抽象接口（与 `VideoEncoder`/`AudioEncoder` 平级，参考 Android MediaCodec
中编码与封装分离的设计），由 FFmpeg backend 提供首个实现。`Muxer` 继承 `PacketSink`
（`core/packet_sink.h` 迁移而来），使 `PacketSource::Await(*muxer)` 可直接投递包，
无需任何消费端适配器。删除现有 `mux/` 模块与 `consumer/Mp4Consumer`——封装职责归入
api 层 `Muxer` 接口，字节输出经 `io::ByteSink`（api 前向声明）。同时修正既有
`mux → @ffmpeg` 依赖违规（muxer 移入 backend，FFmpeg 依赖回归"仅 backend"规则）。

## Technical Context

**Language/Version**: C++17 (per spec `001-project-scaffold`)

**Build System**: Bazel 6.5.0

**Primary Dependencies**: 现有 in-repo `core`、`api`、`queue`、`io`、`consumer`；FFmpeg
backend（`@ffmpeg//:ffmpeg_codec*`，含 libavformat）；googletest。`PacketSink` 迁入 `core`
后新增 `core:packet_sink` target。

**Storage**: N/A (library project, no persistent storage)

**Testing**: googletest — 新增 `api/muxer_contract_test`（StubMuxer 验证接口契约：
音频 Push 默认 unsupported、SetOutput 前置条件）与 `backend/ffmpeg/muxer_test`
（真实编码 → queue → `Await(*muxer)` → MemorySink，验证 `ftyp` 开头、首关键帧产出
头+首分片、Finish 尾部完整、非关键帧丢弃）。沿用 encode_push_test 的
`data=["@ffmpeg//:ffmpeg_codec_archive"] + linkopts=-force_load + linkstatic=True`。

**Target Platform**: macOS ARM64 (development), Linux x86_64 (CI/release), Android arm64
(cross-build, MediaCodec backend 不受影响)

**Project Type**: C++ static/shared library with a public C++ API

**Performance Goals**: Muxer 路径每包一次 move（经 PacketSink::Push），无额外拷贝；
分片字节经 `avio` 缓冲收集、关键帧边界提交一次，无逐包系统调用；阻塞式反压自然
节流生产者。

**Constraints**:
- `api` MUST NOT depend on `queue`/`io`/`consumer` — `PacketSink` 迁入 `core` 使 api 可
  继承；`ByteSink` 在 api 中仅前向声明（`SetOutput(ByteSink*)`）。
- `muxer` 是 backend 能力：仅 `backend/*` 可依赖 `@ffmpeg`。现有 `mux → @ffmpeg` 边
  违规，本次迁移修正。
- 编码器保持协议无关：`VideoConfig` 不引用 `MuxerConfig`；接线由调用方组装
  `encoder → queue → muxer → ByteSink`（FR-005 / US3-A2）。
- Muxer 非线程安全（同 encoder 先例）；容器在首个关键帧懒打开，先前非关键帧丢弃。
- `Mp4Muxer`/`Mp4Consumer` 专用类移除，不再作为对外组件（FR-009）。
- 编码器默认行为不变（不启用封装时输出裸码流），现有测试保持通过。

**Scale/Scope**: 新增 `core/packet_sink.h`、`api/muxer.h`、`backend/ffmpeg/ffmpeg_muxer.{h,cc}`
及 2 个测试；修改 `core/types.h`、`api/codec_factory.{h,cc}`、`queue/queue_iface.h`、
`consumer/packet_consumer.h`、`backend/ffmpeg/register.cc`、example；删除 `mux/` 目录、
`consumer/mp4_consumer.{h,cc}`。

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Constitution file (`.specify/memory/constitution.md`) is the placeholder template only —
no project-specific principles, constraints, or gates defined. Consistent with prior
features (spec 004), the constitution gate is vacuous; project conventions documented in
`codec/doc/architecture/module-dependencies.md` and build-conventions are the effective
constraints (dependency direction, one cc_library per module, alwayslink on register,
visibility). This plan complies: api→core unchanged; backend/ffmpeg→io/ffmpeg new edges
acyclic; mux→@ffmpeg violation removed.

## Project Structure

### Documentation (this feature)

```text
specs/005-muxer-encoder-layering/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output (/speckit.plan command)
├── data-model.md        # Phase 1 output (/speckit.plan command)
├── quickstart.md        # Phase 1 output (/speckit.plan command)
├── contracts/           # Phase 1 output (/speckit.plan command)
└── tasks.md             # Phase 2 output (/speckit.tasks command - NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
codec/src/framework/
├── core/
│   ├── types.h              # + MuxFormat, MuxerConfig
│   ├── packet_sink.h        # (new) PacketSink 迁入（自 queue/queue_iface.h）
│   └── BUILD.bazel          # + packet_sink target
├── api/
│   ├── muxer.h              # (new) 通用 Muxer 抽象（继承 PacketSink）
│   ├── codec_factory.h/.cc  # + RegisterMuxer / CreateMuxer / Muxer::Create
│   └── BUILD.bazel          # + muxer target
├── queue/
│   ├── queue_iface.h        # include core/packet_sink.h；保留 PacketSource/Backpressure
│   └── BUILD.bazel          # dep 调整
├── consumer/
│   ├── packet_consumer.h    # include core/packet_sink.h
│   ├── mp4_consumer.{h,cc}  # (delete)
│   └── BUILD.bazel          # 移除 mp4_consumer
├── backend/ffmpeg/
│   ├── ffmpeg_muxer.{h,cc}  # (new) 移植 mp4_muxer 逻辑，实现 Muxer
│   ├── register.cc          # + RegisterMuxer(Backend::kFFmpeg, ...)
│   └── BUILD.bazel          # + muxer target（deps: api:muxer, core, io:byte_sink, @ffmpeg）
├── mux/                     # (delete) mp4_muxer.{h,cc}, BUILD
└── io/                      # 不变（ByteSink 契约）

codec/src/examples/
├── ffmpeg_encode_file.cc    # 改用 CreateMuxer + SetOutput + Await(*muxer)
└── BUILD.bazel              # deps 调整

codec/tests/
├── api/muxer_contract_test.cc    # (new)
├── backend/ffmpeg/muxer_test.cc  # (new)
└── 各 BUILD.bazel
```

**Structure Decision**: 沿用框架既有"api 抽象 + backend 实现"分层（spec 004 同款）。
`PacketSink` 自 `queue` 迁入 `core` 是唯一结构性调整，使 api 可继承它且保持 api→core
依赖不变。muxer 归属 backend（修正 mux→@ffmpeg 违规），删除独立 mux 模块。

## Complexity Tracking

> 本功能不引入超出既有模式的复杂度：无新构建范式、无跨 backend 依赖、无新抽象层次。
> Constitution Check 无违规，本表留空。

## Phase 0: Research

见 `research.md`——解析关键设计决策（PacketSink 迁 core、接口形态、工厂扩展、输出
目标契约、mux 模块处置、测试策略）。

## Phase 1: Design & Contracts

见 `data-model.md`（Muxer 实体、MuxerConfig、接线关系）、`contracts/muxer-contract.md`
（接口契约）、`quickstart.md`（示例走法）。Phase 1 完成后更新 `CODEBUDDY.md` 的
`<!-- SPECKIT -->` 引用至本 plan.md。
