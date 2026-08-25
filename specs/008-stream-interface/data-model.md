# Data Model: Stream Interface

## Entity: Stream

Represents a single push-stream session. Manages the lifecycle, backend binding, and media flow.

| Field | Type | Description |
|-------|------|-------------|
| id | string | Unique session identifier |
| config | StreamConfig | Active configuration |
| backend | StreamBackend* | Bound transport backend |
| state | StreamState | Current lifecycle state |
| status | StreamStatus | Current observability data |
| abr_controller | ABRController* | Adaptive bitrate controller |
| reconnect_handler | ReconnectHandler* | Auto-reconnect logic |
| buffer | RingBuffer* | Local cache for disconnected periods |

### Stream State Machine (FR-006)

```
Created → Configured → Connecting → Streaming → Disconnected → Destroyed
                ↑              ↓              ↑
                └─── Reconnecting ───────────┘
```

**States**:
- `Created`: Instance allocated, not yet configured
- `Configured`: Config applied, backend selected
- `Connecting`: Backend establishing connection (WHIP signaling)
- `Streaming`: Actively pushing media data
- `Reconnecting`: Network lost, attempting auto-reconnect (FR-015)
- `Disconnected`: Terminal disconnect (clean stop or unrecoverable error)
- `Destroyed`: Resources released

**Invalid transitions** (return error, not crash):
- `Encode` before `Configured`
- `Configure` while `Streaming`
- `Destroy` while holding a callback reference

---

## Entity: StreamConfig

Configuration parameters for a stream session.

| Field | Type | Description |
|-------|------|-------------|
| backend_type | string | Backend identifier (e.g., "webrtc") |
| remote_url | string | WHIP endpoint URL |
| video_codec | string | Codec name (e.g., "h264") |
| audio_codec | string | Codec name (e.g., "opus") |
| initial_bitrate_kbps | uint32 | Starting bitrate for ABR |
| max_bitrate_kbps | uint32 | Maximum bitrate cap |
| min_bitrate_kbps | uint32 | Minimum bitrate floor |
| resolution_width | uint32 | Target video width |
| resolution_height | uint32 | Target video height |
| framerate | uint32 | Target frame rate |
| buffer_duration_s | uint32 | Local buffer duration on disconnect (default 30) |
| reconnect_max_interval_s | uint32 | Max reconnect backoff interval (default 30) |
| stun_server | string | Optional STUN server URL |
| turn_server | string | Optional TURN server URL |

---

## Entity: StreamBackend (Abstract Interface)

Contract that each transport backend implements.

| Method | Signature | Description |
|--------|-----------|-------------|
| `Connect` | (config: StreamConfig) → Status | Establish connection to remote endpoint |
| `SendVideo` | (packet: VideoPacket) → Status | Push encoded video frame |
| `SendAudio` | (packet: AudioPacket) → Status | Push encoded audio frame |
| `Disconnect` | () → Status | Gracefully teardown connection |
| `GetStats` | () → StreamStats | Return transport statistics (RTT, packet loss, jitter) |
| `OnStatusChange` | (callback: StatusCallback) → void | Register status change callback |

---

## Entity: StreamStatus

Observability data for a stream session.

| Field | Type | Description |
|-------|------|-------------|
| state | StreamState | Current lifecycle state |
| bitrate_kbps | uint32 | Current sending bitrate |
| rtt_ms | uint32 | Round-trip time |
| packet_loss_pct | float | Packet loss percentage |
| bytes_sent | uint64 | Total bytes transmitted |
| frames_sent | uint64 | Total frames transmitted |
| frames_dropped | uint64 | Frames dropped due to backpressure |
| uptime_s | uint64 | Session duration in seconds |
| last_error | string | Last error message (if any) |

---

## Entity: ABRController

Adaptive bitrate logic (FR-007).

| Field | Type | Description |
|-------|------|-------------|
| current_bitrate_kbps | uint32 | Current target bitrate |
| min_bitrate_kbps | uint32 | Floor |
| max_bitrate_kbps | uint32 | Ceiling |
| last_stats | StreamStats | Last observed transport stats |

**Behavior**: Periodically samples transport stats (RTT, packet loss). If packet loss exceeds threshold, decreases bitrate. If conditions improve, increases bitrate gradually.

---

## Entity: ReconnectHandler

Auto-reconnect with exponential backoff (FR-015, FR-016).

| Field | Type | Description |
|-------|------|-------------|
| max_interval_s | uint32 | Max backoff interval |
| current_attempt | uint32 | Attempt counter |
| buffer | RingBuffer | Local cache during disconnection |

**Behavior**: On disconnect, starts recording to ring buffer. Attempts reconnect with exponential backoff. On success, either drains buffer or flushes it and continues real-time.

---

## Entity: TestServer

Lightweight WHIP test server (FR-012, FR-013).

| Field | Type | Description |
|-------|------|-------------|
| port | uint16 | HTTP server port |
| whip_endpoint | string | WHIP resource path |
| active_sessions | map<string, WhipSession> | Active push sessions |
| player_page | string | Embedded HTML for browser player |