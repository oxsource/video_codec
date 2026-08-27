# Feature Specification: Stream Interface

> **Status: historical spec.** The requirements below are frozen; the concrete
> data model, build layout, and verification flow have evolved. See
> [data-model.md](data-model.md), [quickstart.md](quickstart.md),
> [webrtc-backend-context.md](webrtc-backend-context.md) and `stream/README.md`
> for the implementation as it stands today.

**Feature Branch**: `008-stream-interface`

**Created**: 2026-08-24

**Status**: Draft

**Input**: User description: "新增提案，补充推流相关实现，架构上参考codec设计，设计统一的接口，然后由不同的backend实现，一期暂时仅有webrtc实现。stream建议可以和codec层级平行保持独立。"

## Clarifications

### Session 2026-08-24

- Q: 需要配套的测试服务器，类型是什么？ → A: 轻量 WHIP 测试服务器，提供浏览器播放页面用于验证推流。server 不是库的核心交付物。
- Q: 测试服务器如何集成？ → A: 独立进程，在 repo 中作为独立可执行文件编译。
- Q: 推流是否需要自适应码率以应对移动网络波动？ → A: 是，需要自适应码率 (ABR)。
- Q: 车辆进入隧道等信号丢失场景如何处理？ → A: 自动重连 + 本地缓存，恢复后从断点续推或丢弃过期缓存继续实时流。

## User Scenarios & Testing

### User Story 1 - Push Media Stream via Unified Interface (Priority: P1)

A developer wants to push encoded media (video/audio) to a remote endpoint. They use the stream module's unified interface, which abstracts away the underlying transport. In v1, the only available backend is WebRTC.

**Why this priority**: This is the core value proposition — the ability to push streams. Without this, no streaming functionality exists.

**Independent Test**: Can be fully tested by pushing a pre-encoded H.264 + AAC stream to a WebRTC-compatible server and verifying the remote side receives playable media.

**Acceptance Scenarios**:

1. **Given** a stream interface initialized with WebRTC backend, **When** a developer calls `Start()` with encoded media data, **Then** the stream is successfully transmitted to the configured remote endpoint
2. **Given** an active stream, **When** the developer calls `Stop()`, **Then** the stream is cleanly terminated and the remote endpoint receives an end-of-stream signal
3. **Given** a network interruption during streaming, **When** connectivity is restored, **Then** the stream automatically recovers or reports a clear error status

---

### User Story 2 - Backend Selection and Lifecycle Management (Priority: P2)

A developer configures which streaming backend to use at initialization. The system selects the appropriate backend (WebRTC for v1) and manages its lifecycle (create, connect, stream, disconnect, destroy).

**Why this priority**: The backend abstraction is the foundation for multi-transport support. Though only one backend exists in v1, the architecture must be proven.

**Independent Test**: Can be tested by switching between a valid WebRTC backend and an invalid/unsupported backend, verifying the system handles both correctly.

**Acceptance Scenarios**:

1. **Given** the stream interface, **When** a developer specifies "webrtc" as the backend type, **Then** the WebRTC backend is instantiated and ready for use
2. **Given** the stream interface, **When** a developer specifies an unsupported backend type, **Then** the system returns a clear error indicating the backend is not available
3. **Given** a stream session, **When** the session ends, **Then** all backend resources are released

---

### User Story 3 - Stream Configuration and Parameter Tuning (Priority: P3)

A developer configures stream parameters such as remote URL, codec type, bitrate, resolution, and framing settings through a unified configuration object.

**Why this priority**: Configuration is essential for real-world usage but can be iterated on after the basic stream path works.

**Independent Test**: Can be tested by providing different configurations and verifying the stream behavior matches expectations (e.g., different resolutions, bitrates).

**Acceptance Scenarios**:

1. **Given** a stream configuration with specific video/audio parameters, **When** the stream starts, **Then** the output matches the configured parameters
2. **Given** a stream configuration with missing optional parameters, **When** the stream starts, **Then** sensible defaults are applied

---

### User Story 4 - Run Test Server for Validation (Priority: P2)

A developer wants to validate the stream push functionality end-to-end. They run a lightweight WHIP test server (standalone binary) that receives the pushed stream and serves a browser page for subscribing and viewing the live stream.

**Why this priority**: The test server is essential for verification and CI, but it's not the core library deliverable.

**Independent Test**: Can be fully tested by starting the test server, pushing a stream via the stream interface, and viewing the stream in a browser on the same machine.

**Acceptance Scenarios**:

1. **Given** the test server is running, **When** a developer pushes an H.264 stream via the WebRTC backend, **Then** the server receives the stream and the browser page displays the live video
2. **Given** the test server is running, **When** a developer opens the browser page at `http://localhost:<port>`, **Then** the page shows a live stream player with connection status
3. **Given** the test server and a push stream, **When** the push stops, **Then** the browser page displays a "stream ended" state

---

### Edge Cases

- What happens when the remote endpoint is unreachable or rejects the connection?
- How does the system handle rapid start/stop/start cycles?
- What happens when encoded media arrives faster than the transport can send?
- How does the system behave when the stream interface is destroyed mid-stream?
- What happens if the codec output format changes mid-stream (e.g., resolution change)?
- **Vehicle entering tunnel**: complete network loss for 30+ seconds — how does the stream recover when signal returns?
- **Cellular handover**: IP address change during cell tower switch — does the WebRTC ICE connection survive?
- **Carrier throttling**: mobile carrier reduces bandwidth after sustained usage — how does ABR respond?

## Requirements

### Functional Requirements

- **FR-001**: System MUST provide a unified stream interface that is independent of the underlying transport backend
- **FR-002**: The stream interface MUST be positioned as a peer module to the codec layer, with no circular dependency between them
- **FR-003**: System MUST support a backend plugin architecture, where each backend implements the same stream interface contract
- **FR-004**: System MUST include a WebRTC backend as the initial (v1) transport implementation
- **FR-005**: The WebRTC backend MUST support pushing encoded video (H.264) and audio (AAC/Opus) streams to a remote endpoint using the WHIP (WebRTC HTTP Ingestion Protocol) for connection establishment
- **FR-006**: System MUST provide a stream lifecycle: Created → Configured → Connecting → Streaming → Disconnected → Destroyed
- **FR-007**: System MUST support adaptive bitrate (ABR) — the stream module MUST dynamically adjust encoding parameters (bitrate, resolution, frame rate) based on real-time network conditions reported by the transport backend
- **FR-008**: System MUST support backpressure handling — when the transport cannot keep up with the encoder output, the system MUST either buffer with a configurable limit or drop frames
- **FR-009**: System MUST report stream status and errors through a uniform callback interface
- **FR-010**: System MUST support a single stream session per interface instance; multiple concurrent sessions require multiple instances
- **FR-011**: System MUST allow the stream module to consume encoded media from the codec module's output
- **FR-012**: System MUST include a lightweight WHIP test server as a standalone binary for end-to-end testing
- **FR-013**: The test server MUST serve a browser-based player page for subscribing and viewing the live stream
- **FR-015**: System MUST support automatic reconnection with exponential backoff when the network connection is lost
- **FR-016**: System MUST support local buffering during network disconnection; when the connection is restored, the system MUST either resume from the buffered point or flush the buffer and continue with real-time streaming

### Key Entities

- **Stream**: Represents a single push-stream session. Manages the lifecycle, backend binding, and media flow.
- **StreamBackend**: Abstract interface that each transport backend (WebRTC, etc.) implements. Defines connect, send, disconnect, and status reporting.
- **StreamConfig**: Configuration parameters for a stream session, including remote endpoint, media format, codec parameters, and backend selection.
- **StreamStatus**: Observability object reporting current state, statistics (bytes sent, packets lost, round-trip time), and error information.
- **TestServer**: Lightweight WHIP-compatible server binary. Receives pushed streams and serves a browser player page for live viewing. Not part of the core library — a standalone tool for development and CI validation.

## Success Criteria

### Measurable Outcomes

- **SC-001**: A developer can push a complete H.264 video stream to a standard remote endpoint in under 30 minutes of integration effort
- **SC-002**: The stream module can sustain a 720p30 stream over a 4G mobile network (simulated 2 Mbps variable bandwidth) without dropping more than 3% of frames
- **SC-003**: Adding a new transport backend (e.g., RTMP, SRT) requires no changes to the stream interface or codec module — only a new backend implementation
- **SC-004**: Stream startup (from call to connected) completes in under 3 seconds on a local network
- **SC-005**: The stream module can be compiled and tested independently from any specific transport backend, with a mock backend for unit testing

## Assumptions

- The codec module (existing) produces encoded media in a format that the stream module can consume directly
- The development environment has access to a WebRTC library (e.g., libwebrtc, or a system-level WebRTC stack)
- Network infrastructure (STUN/TURN servers if needed for WebRTC) is available externally and not provided by this module
- v1 focuses on push (encoding → sending) only; pull (receiving/playing) is out of scope (except for the test server's browser player page)
- The stream module does not handle media encoding — it only transports pre-encoded media
- WebRTC signaling server is assumed to be available and compatible with the chosen signaling protocol
- The test server is a standalone binary, not part of the core stream library; it is built separately and used only for development and CI
- The primary deployment environment is vehicle-mounted devices connected via 4G/5G mobile networks, with typical bandwidth of 1–10 Mbps and potential for frequent signal loss