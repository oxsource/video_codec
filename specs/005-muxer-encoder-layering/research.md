# Research: Muxer 与 Encoder 分层设计

**Branch**: `005-muxer-encoder-layering` | **Date**: 2026-08-13 | **Plan**: [plan.md](plan.md)

Phase 0 调研结论——解析 plan Technical Context 中的设计决策。

## R1 — PacketSink 迁移到 core

**Decision**: 将 `PacketSink` 从 `queue/queue_iface.h` 迁至新文件 `core/packet_sink.h`；
`queue_iface.h` include 它并保留 `PacketSource`/`Backpressure`；`consumer/packet_consumer.h`
改 include `core/packet_sink.h`。

**Rationale**: `Muxer` 接口需继承 `PacketSink` 才能让 `PacketSource::Await(*muxer)`
直接投递（无适配器）。但 `api` 不能依赖 `queue`（module-dependencies 固定 `api → core
only`）。`PacketSink` 是纯接口，仅用 core 类型（`VideoPacket`/`AudioPacket`/`Status`），
迁移到 core 无语义变化，且使 api 可完整 include 并继承。

**Alternatives considered**:
- api 前向声明 `PacketSink` 并继承：继承需要完整定义，前向声明不可行。
- `Muxer` 独立接口 + 消费端薄适配器：重新引入包装类，与 FR-009（移除 Mp4Consumer）
  相悖，且 Await 无法直接衔接。

## R2 — Muxer 接口形态

**Decision**: `Muxer : public PacketSink`，新增 `SetOutput(ByteSink*)` 与 `Release()`；
`Push(VideoPacket&&)` 为纯虚，`Push(AudioPacket&&)` 默认返回 `kUnsupportedOperation`
（v1 仅视频）；`Flush()`/`Finish()` 纯虚。无 `Init()`——容器在首个关键帧懒打开。

**Rationale**: 融合三处既有模式：PacketSink 的 Push/Flush/Finish（消费端衔接）、
Mp4Muxer 的懒打开（首关键帧需 SPS/PPS）、VideoEncoder 的 SetOutput/Release 生命周期。
懒打开避免空流构造失败的 Init 阶段。

**Alternatives considered**: 保留独立 Init（空流仍需合法状态，徒增状态机）；Muxer 不
继承 PacketSink（失去直接 Await 衔接）。

## R3 — 输出目标：io::ByteSink + api 前向声明

**Decision**: Muxer 字节输出走 `io::ByteSink`，通过 `virtual Status SetOutput(ByteSink* sink)`
注入（非拥有，必须早于首次 Push；nullptr 解除）。api 中仅 `class ByteSink;` 前向声明。

**Rationale**: 与现有 `SetOutputSink(PacketSink*)` 对称；工厂 Create 签名保持统一、
可运行时重定向；api 不编译依赖 io（同 PacketSink fwd-declare 先例）。字节输出沿用
既定 io 层契约（spec Assumption）。

**Alternatives considered**: 构造参数传 sink——工厂需感知 io 类型且不可重定向；
内部 FragmentEmitter 回调——脱离 io 层契约，回到被否定的"内存收集"方案。

## R4 — 工厂机制扩展

**Decision**: 扩展现有 `api/encoder_factory.{h,cc}`，不新建 MuxerFactory。新增
`using MuxerCreator = std::function<std::unique_ptr<Muxer>(const MuxerConfig&)>;`
`RegisterMuxer(Backend, MuxerCreator)`、`CreateMuxer(const MuxerConfig&)`，`Muxer::Create`
在 encoder_factory.cc 中转发。Registry 增加 `unordered_map<Backend, MuxerCreator> mux`。

**Rationale**: 复用现有按 Backend 键的注册表与 `ResolveBackend` 平台选择，与
Video/Audio 编码器完全对称，零新增抽象。

**Alternatives considered**: 独立 MuxerFactory——重复注册表/锁/选择逻辑，无收益。

## R5 — MuxerConfig 设计

**Decision**: `enum class MuxFormat { kMp4 };`（v1）与 `struct MuxerConfig { MuxFormat
format; bool fragmented=true; int width/height/fps; Backend backend; IsValid(); }`
放入 `core/types.h`，与 Video/AudioEncoderConfig 同居。`VideoEncoderConfig` 不引用
MuxerConfig。

**Rationale**: 编码器保持协议无关（FR-005/US3-A2）；接线由调用方组装
`encoder→queue→muxer→sink`。MuxFormat 枚举比 MediaFileFormat 扩展名字符串更适合
内部路由（backend 选择、IsValid 校验），且 core 是 leaf 不能依赖 utils。

**Alternatives considered**: 复用 utils::MediaFileFormat 字符串——core 不能依赖 utils；
`VideoEncoderConfig` 内嵌容器字段——破坏编码器协议无关。

## R6 — mux 模块处置

**Decision**: 删除 `mux/` 整目录与 `consumer/mp4_consumer.{h,cc}`。Mp4Muxer 逻辑移植为
`backend/ffmpeg/ffmpeg_muxer.{h,cc}`（实现 Muxer 接口），FFmpeg 依赖随迁。

**Rationale**: mux 模块直接依赖 `@ffmpeg` 违反"仅 backend 可依赖 @ffmpeg"规则
（module-dependencies 文档自相矛盾，本次修正）。MP4 封装是 FFmpeg 系能力，归入
backend 正确。FR-009 要求移除专用封装组合，Muxer 接口取代。

**Alternatives considered**: 保留 mux 作为框架级模块并继续依赖 @ffmpeg——维持违规；
保留 Mp4Consumer 作为兼容层——违反 FR-009。

## R7 — 测试策略

**Decision**: 两层测试：
1. `tests/api/muxer_contract_test.cc`：StubMuxer 验证接口契约（音频 Push 默认
   unsupported、SetOutput 前置条件、Finish/Release 幂等）。
2. `tests/backend/ffmpeg/muxer_test.cc`：真实编码（SMPTE 帧）→ `encoder->SetOutputSink(&q)`
   → `q.Await(*muxer)` → MemorySink（ByteSink 实现），断言首字节 `ftyp`（0x66 0x74 0x79
   0x70）、首关键帧产出"头+首分片"、Finish 后含 moov/mdat、非关键帧丢弃。

**Rationale**: 接口契约与真实后端分离验证（同 encoder_lifecycle_test /
encode_push_test 分层）。集成测试沿用 encode_push_test 的 force_load 配置
（`data=["@ffmpeg//:ffmpeg_codec_archive"] + linkopts=-force_load + linkstatic=True`）。

**Alternatives considered**: 仅集成测试——契约错误暴露晚；仅契约测试——真实 muxer
路径无覆盖。

## R8 — example 走法

**Decision**: `ffmpeg_encode_file.cc`：默认（非 --raw）路径改为
`CreateMuxer(cfg)` → `muxer->SetOutput(file_byte_sink)` →
`queue.Await(*muxer)`；`--raw` 分支保留 `FileSinkConsumer`。删除 Mp4Consumer/FileByteSink
组合构造。

**Rationale**: Muxer 继承 PacketSink，Await 直接衔接，无需消费者包装；example 展示
推荐接线（encoder→queue→muxer→sink）。
