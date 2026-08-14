# Feature Specification: Input Surface（Android MediaCodec 输入面）

**Feature Branch**: `007-input-surface-api`

**Created**: 2026-08-14

**Status**: Draft

**Input**: User description: "接下来针对input_surface设计方案，如果有可能，建议针对非安卓平台也提供统一的surface接口，本质就是codec提供内存buffer，然后由调用者进行绘制然后再提交到codec作为VideoFrame"

**Scope revision (2026-08-14)**: 非安卓平台 Surface 弃用；v1 仅实现 Android MediaCodec `createInputSurface`（硬件输入面）。更简洁灵活，非安卓后端明确返回不支持。

**API simplification (2026-08-14)**: `CreateInputSurface()` 直接返回原生句柄 `void*`（Android 上为 `ANativeWindow*`），不再有 `InputSurface` 类；`input_surface.h` 删除。调用者直接向该原生窗口绘制，系统自动投递缓冲。

## User Scenarios & Testing *(mandatory)*

<!-- 本功能面向 video_codec 库的使用者（应用开发者）。
     v1 范围（经澄清）：仅 Android 输入面——编码器通过统一 API 返回系统硬件
     输入面的原生句柄（MediaCodec createInputSurface / ANativeWindow），
     调用者直接绘制后由系统投递编码。非安卓后端返回 nullptr。 -->

### User Story 1 - Android 硬件输入面（绘制→系统投递→编码） (Priority: P1)

Android 应用开发者（渲染引擎、游戏、录屏）希望把渲染画面直接交给系统硬件编码器，避免"渲染 → 拷回 CPU → 重新上传"的往返开销。开发者从编码器获得原生绘制句柄，向其中绘制一帧，系统自动把缓冲投递给编码器，产出 H.264 码流。非安卓平台调用同一接口得到 `nullptr`，开发者按框架惯例回退到 CPU 帧路径。

**Why this priority**: 这是本功能的全部价值——Android 上打通"系统硬件输入面"闭环，兑现 `CreateInputSurface()` 自 v1（spec 006）悬置的能力。非安卓侧不做软件面（经澄清），因此本故事即 MVP 全部。

**Independent Test**: 在 Android 设备上：创建编码器 → `Init()` → 调用 `CreateInputSurface()`（返回非空原生句柄）→ 向句柄绘制 SMPTE 彩条（含时间戳）→ 冲刷 → 得到可被标准播放器解码的 H.264 码流；在宿主机调用同一代码得到 `nullptr`。

**Acceptance Scenarios**:

1. **Given** 应用运行在 Android 设备上且编码器已 `Init()`，**When** 开发者调用 `CreateInputSurface()`，**Then** 返回非空原生句柄（系统硬件输入面）
2. **Given** 应用运行在宿主（FFmpeg 后端），**When** 开发者调用 `CreateInputSurface()`，**Then** 返回 `nullptr`（明确不支持），不崩溃、不产生部分初始化状态
3. **Given** 调用者向句柄绘制并提交了 60 帧（带递增时间戳），**When** 冲刷编码器，**Then** 产出完整 H.264 码流，可解码播放，帧序与时间戳正确
4. **Given** 编码器输出接 push 模式与 muxer，**When** 输入面路径完整运行，**Then** 产出合法 MP4，与 CPU 帧路径输出语义一致

---

### User Story 2 - 系统背压与缓冲语义 (Priority: P2)

实时渲染帧率可能高于编码吞吐；开发者期望缓冲语义由系统保障（原生窗口自带缓冲队列），提交过快时系统阻塞或拒绝，框架不引入额外无界缓冲。

**Why this priority**: 生产环境的录制/直播依赖有界背压防内存膨胀。Android 硬件面的缓冲队列由系统管理（通常 2–4 块），框架只需透传语义、不重复实现缓冲池。

**Independent Test**: 以超过编码吞吐的速率持续绘制提交 3 倍时长，进程内存占用有界、无 OOM；编码完成后码流完整无帧错乱。

**Acceptance Scenarios**:

1. **Given** 调用者以高于编码吞吐的速率绘制提交，**When** 系统缓冲队列已满，**Then** 绘制侧按系统语义阻塞或失败，框架不额外缓冲
2. **Given** 编码器持续消费，**When** 缓冲归还，**Then** 绘制提交恢复，无帧丢失
3. **Given** 绘制期间发生编码器失败/重置，**When** 调用者继续向句柄绘制，**Then** 操作被明确拒绝（句柄失效），不崩溃

---

### User Story 3 - 生命周期、EOS 与输入模式互斥 (Priority: P3)

开发者期望输入面句柄与编码器生命周期一致，且明确"CPU 帧"与"输入面"两种输入模式的边界：同一编码器实例激活输入面后，CPU 帧输入被拒绝（或反之）；冲刷（Flush）能正确触发输入面路径的结束信号并输出剩余码流。

**Why this priority**: 输入模式互斥与 EOS 语义决定 API 的正确性与可组合性——开发者必须知道何时可用输入面、如何结束输入面流。

**Independent Test**: 在 Android 设备上分别验证：未 `Init()` 请求输入面、`Release()` 后继续向句柄绘制、同一实例混用两种输入、输入面路径下 `Flush()` 触发结束信号并完整输出剩余帧。

**Acceptance Scenarios**:

1. **Given** 编码器尚未 `Init()`，**When** 调用 `CreateInputSurface()`，**Then** 返回明确"未初始化"错误（或 `nullptr` + 错误状态）
2. **Given** 编码器已 `Release()`，**When** 继续向句柄绘制，**Then** 操作被明确拒绝，不崩溃
3. **Given** 同一编码器实例已激活输入面，**When** 调用者调用 `Encode(VideoFrame)`，**Then** 返回明确错误（输入模式互斥）
4. **Given** 输入面路径下调用 `Flush()`，**When** 结束信号送达编码器，**Then** 剩余帧全部输出，状态可安全回收

---

### Edge Cases

- 调用者对同一缓冲重复绘制未释放——系统 MUST 拒绝或保持幂等，不得产生未定义行为。
- 提交后调用者仍访问缓冲数据——系统 MUST 将该缓冲标记为系统/编码器所有，访问行为不保证（文档明确契约）。
- Android 设备不支持 `createInputSurface`（创建失败）——`CreateInputSurface()` MUST 返回 `nullptr` + 明确错误状态，不静默降级、不产生部分初始化状态。
- 首帧非关键帧——与既有 muxer 契约一致（首个关键帧前丢弃），输入面路径不得破坏该行为。
- 编码器失败/重置时在途缓冲——系统 MUST 回收并使后续操作导向明确错误状态。
- 空绘制（未写入内容）——按正常帧提交或明确拒绝，二选一并文档化，不得静默产生损坏码流。
- 输入模式互斥被违反（surface 激活后调 CPU 输入）——明确拒绝（MediaCodec 输入模式约束）。

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Android 后端 MUST 使 `VideoEncoder::CreateInputSurface()` 返回非空原生句柄（`ANativeWindow*`，经 MediaCodec `createInputSurface` 获得，零拷贝）。
- **FR-002**: 非安卓后端（FFmpeg）MUST 使 `CreateInputSurface()` 返回 `nullptr`（明确不支持），不崩溃、不产生部分初始化状态。
- **FR-003**: 返回句柄 MUST 向调用者暴露可绘制的缓冲语义（像素格式、尺寸、stride 可查询）；调用者据此绘制。
- **FR-004**: 绘制完成的帧 MUST 由系统自动投递给编码器（原生窗口投递语义）；时间戳 MUST 由调用者经句柄/平台机制在绘制时设定。
- **FR-005**: 输入面的缓冲队列与背压 MUST 由系统保障（原生窗口自带队列）；框架 MUST NOT 引入额外的无界缓冲。
- **FR-006**: 输入面路径产出的包 MUST 与 `Encode(VideoFrame)` 共用同一输出管线：关键帧标记、PTS、包序语义 MUST 一致，下游消费（muxer/文件）无需感知输入来源。
- **FR-007**: 输入面句柄生命周期 MUST 与编码器状态机集成：仅 `Initialized`（含）之后可获取；`Flush`/`Release` 后句柄 MUST 失效，后续操作 MUST 返回明确错误。
- **FR-008**: 输入面路径的结束信号（EOS）MUST 由 `Flush()` 触发并送达编码器（对应系统输入流结束语义），剩余帧 MUST 全部输出。
- **FR-009**: 同一编码器实例的"CPU 帧输入（`Encode(VideoFrame)`）"与"输入面输入"MUST 互斥：激活其一后使用另一个 MUST 返回明确错误（MediaCodec 输入模式约束）。
- **FR-010**: 删除 `InputSurface` 类及 `input_surface.h`；API 不再暴露任何 surface 抽象类型（仅 `void*` 原生句柄）。

### Key Entities *(include if feature involves data)*

- **VideoEncoder**: 提供输入面原生句柄（`CreateInputSurface`）并消费其投递帧的编码器；维持编码状态机与输出管线。
- **原生绘制句柄（Native Surface Handle）**: `CreateInputSurface()` 返回的不透明句柄；Android 上为 `ANativeWindow*`，承载缓冲队列与绘制语义。
- **VideoPacket**: 输入面投递帧编码后产出的包；与 `Encode(VideoFrame)` 产出的包同构（数据、PTS、关键帧标记）。

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 在 Android 设备上，调用者通过统一接口完成"获取句柄 → 绘制 → 冲刷"并产出合法 H.264 码流；同一代码在宿主机得到 `nullptr`。
- **SC-002**: 连续绘制提交 60 帧（640x480）的编码输出可被标准播放器/解码器完整解码，无帧丢失、时间戳单调。
- **SC-003**: 以超过编码吞吐 3 倍的速率绘制提交时，进程内存占用有界（不超过系统缓冲队列对应量级），无 OOM、无无界增长。
- **SC-004**: 非法操作（未初始化获取、Release 后绘制、违反输入互斥）100% 被明确拒绝，不产生损坏输出或崩溃。
- **SC-005**: 新增输入面路径后，既有 `Encode(VideoFrame)` CPU 路径与全部现有测试无回归。

## Assumptions

- v1 输入面为 **Android 专属**能力：仅 MediaCodec `createInputSurface` 硬件面；非安卓后端返回 `nullptr`，不做软件内存面（用户澄清 2026-08-14）。
- API 形态为最小化：`CreateInputSurface()` 返回 `void*`（Android 为 `ANativeWindow*`），删除 `InputSurface` 类与 `input_surface.h`（用户澄清 2026-08-14）。
- 调用者负责向句柄绘制并设定每帧时间戳（CPU 绘制可经窗口时间戳接口/平台机制；GPU 绘制由调用者经 EGL 设定），框架仅保证句柄有效期内投递与 EOS 语义。
- Android 输入面的缓冲队列与背压由系统保障，框架不实现自有缓冲池。
- "CPU 帧输入"（`Encode(VideoFrame)`）与"输入面输入"在同一编码器实例上互斥（MediaCodec 语义约束）。
- 复用既有 `EncoderLifecycle` 状态机、`CodecFactory` 后端解析与输出管线，不引入新的传输/线程模型。
- 依赖 Android 系统既有输入面能力（API 21+），不引入额外平台原生依赖。
- 调用者每绘制一帧后应泵送编码器输出（`Poll()`）——硬件编码器在输出未排空时停止消费输入面缓冲（背压，研究 R7）；这是 surface 流的标准用法，非可选优化。

## Clarifications

### Session 2026-08-14

- Q1（specify 阶段，像素格式与缓冲模型）: 非 Android 平台 Surface 应维护内存 Buffer 分区（至少双缓冲），格式由调用者指定；绘制与投递为生产-消费模型。 → A（初版采纳，后被下述范围修订推翻）: 非 Android 输入面 = 调用者指定格式 + 内存缓冲池。
- Q2（specify 阶段，Android 实现路径）: Android 平台的 Surface 实现直接利用 MediaCodec 的 createInputSurface。 → A: 采纳——Android 输入面复用系统硬件输入面（零拷贝）。
- **范围修订（clarify 阶段）**: 非安卓平台的 Surface 弃用；v1 仅实现 MediaCodec 提供的 InputSurface 支持（更简洁灵活）。 → 推翻 Q1 的"非 Android 软件面"部分；Android 部分保留 Q2。
- **API 形态（clarify 阶段）**: `CreateInputSurface()` 直接返回 `void*`（Android 上为 `ANativeWindow*`），`input_surface.h` 删除，不再有 `InputSurface` 类。 → 采纳——最小化接口，调用者直接操作原生句柄。
