# Data Model: Stream Interface

This document reflects the **current implementation** (`stream/` workspace on
branch `008-stream-interface`). Historical/design-only entities that were never
(or no longer) part of the implementation are omitted.

## Entity: Stream

Represents a single push-stream session. Manages the lifecycle, backend binding, and media flow.

| Field | Type | Description |
|-------|------|-------------|
| config | StreamConfig | Active configuration |
| backend | StreamBackend* | Bound transport backend |
| status | StreamStatus | Current observability data |
| abr_controller | AbrController* | Adaptive bitrate controller |
| reconnect_handler | ReconnectHandler* | Auto-reconnect logic |
| backpressure_buffer_count | uint32 | In-flight packet count for backpressure/drop policy |

> The design's `RingBuffer*` local cache is **not implemented**: on disconnect
> the frame is dropped (backpressure counter) instead of buffered. See
> ReconnectHandler below for the disconnect/timeout model actually used.

### Stream State Machine (FR-006)

```
Created → Configured → Connecting → Streaming → Disconnected → Destroyed
                ↑              ↓              ↑
                └─── Reconnecting ───────────┘
```

Implemented as `video::stream::StreamState` in `src/api/stream_status.h`:

- `kCreated`: Instance allocated, not yet configured
- `kConfigured`: Config applied, backend selected
- `kConnecting`: Backend establishing connection (WHIP signaling)
- `kStreaming`: Actively pushing media data
- `kReconnecting`: Network lost, attempting auto-reconnect (FR-015)
- `kDisconnected`: Terminal disconnect (clean stop or unrecoverable error)
- `kDestroyed`: Resources released

**Invalid transitions** are rejected and surfaced via `last_error` + the status
callback (no crash): e.g. `SendVideo`/`SendAudio` before `Configured`, `Start`
before `Init`, config change while `Streaming`.

---

## Entity: StreamConfig

Configuration parameters for a stream session (`src/api/stream_config.h`).

| Field | Type | Description |
|-------|------|-------------|
| backend_type | string | Backend identifier (e.g., `webrtc`) |
| remote_url | string | WHIP endpoint URL (or derived from signal host + path in JSON config) |
| video_codec | string | Codec name (e.g., `h264`) |
| audio_codec | string | Codec name (e.g., `aac`) |
| initial_bitrate_kbps | uint32 | Starting bitrate for ABR (default 2000) |
| max_bitrate_kbps | uint32 | Maximum bitrate cap (default 5000) |
| min_bitrate_kbps | uint32 | Minimum bitrate floor (default 200) |
| resolution_width | uint32 | Target video width (default 1280) |
| resolution_height | uint32 | Target video height (default 720) |
| framerate | uint32 | Target frame rate (default 30) |
| buffer_duration_s | uint32 | Reconnect buffer window on disconnect (default 30) |
| reconnect_max_interval_s | uint32 | Max reconnect backoff interval (default 30) |
| network | NetworkConfig | Transport/TLS settings for WHIP HTTP (see below) |
| stun_server | string | Optional STUN server URL |
| turn_server | string | Optional TURN server URL |

### Entity: NetworkConfig (`src/api/network_config.h`)

Transport/security options for the WHIP HTTP client (mapped onto the
cpp_network http client). Timeouts in ms; `0` = library default.

| Field | Type | Description |
|-------|------|-------------|
| connect_timeout_ms | uint32 | Connection timeout (default 5000) |
| read_timeout_ms / write_timeout_ms | uint32 | Per-read/write timeout (default 0 = default) |
| total_timeout_ms | uint32 | Total request timeout (default 10000) |
| follow_redirects | bool | Follow HTTP redirects (default false) |
| max_redirects | int | Redirect cap (default 20) |
| local_address / local_port | string / uint16 | Optional local interface binding |
| proxy_host / proxy_port | string / uint16 | Optional HTTP(S) proxy |
| keep_alive_ms | uint32 | TCP keep-alive idle time (default 0) |
| tls_verify | bool | Peer certificate verification (default true) |
| tls_ca_file / tls_ca_pem | string | CA as file path or inline PEM (file wins) |
| tls_client_cert / tls_client_key | string | mTLS cert/key (path or PEM, used together) |
| tls_sni | string | Override SNI host |

### JSON Configuration (`StreamConfig::LoadFromFile / ::ParseFromJson`)

Implemented in `src/core/stream_config.cc`; the field-key schema and all
defaults are owned by the module. The WHIP URL is derived from the signal
`host` + `path` as `host + "/" + path + "/whip"` unless an explicit `url` is
given. Example: `src/examples/stream_conf.json`.

---

## Entity: StreamBackend (Abstract Interface)

Contract implemented by each transport backend (`src/api/stream_backend.h`).

| Method | Signature | Description |
|--------|-----------|-------------|
| `Connect` | `(config: StreamConfig) → Status` | Establish connection to remote endpoint |
| `SendVideo` | `(packet: VideoPacket) → Status` | Push encoded video frame |
| `SendAudio` | `(packet: AudioPacket) → Status` | Push encoded audio frame |
| `Disconnect` | `() → Status` | Gracefully teardown connection |
| `GetStats` | `() → StreamStats` | Return transport statistics |
| `SetStatusCallback` | `(callback: StatusCallback) → void` | Register status change callback |

Backends self-register via the `VIDEO_STREAM_REGISTER(name, factory)` macro
(`src/core/video_stream_register.h`); `RegisterBackend`/`CreateBackend` in
`src/core/backend_registry.*`.

### Entity: StreamStats (`src/api/stream_backend.h`)

| Field | Type | Description |
|-------|------|-------------|
| rtt_ms | uint32 | Round-trip time |
| packet_loss_pct | float | Packet loss percentage |
| jitter_ms | uint32 | Jitter |
| bytes_sent | uint64 | Total bytes transmitted |
| packets_sent | uint64 | Total packets transmitted |

---

## Entity: StreamStatus

Observability data for a stream session (`src/api/stream_status.h`).

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

`StatusCallback` is `std::function<void(const StreamStatus&)>`, registered via
`Stream::SetStatusCallback`.

---

## Entity: AbrController

Adaptive bitrate logic (FR-007), `src/core/abr_controller.h`.

| Member | Type | Description |
|--------|------|-------------|
| `CurrentBitrateKbps()` | uint32 | Current target bitrate |
| `MinBitrateKbps()` | uint32 | Floor |
| `MaxBitrateKbps()` | uint32 | Ceiling |
| `Update(rtt_ms, packet_loss_pct)` | void | Adjust bitrate from latest transport stats |
| `Reset()` | void | Return to the initial bitrate |

Constructor: `AbrController(initial_bitrate_kbps, min_bitrate_kbps, max_bitrate_kbps)`.

---

## Entity: ReconnectHandler

Auto-reconnect with exponential backoff (FR-015, FR-016), `src/core/reconnect_handler.h`.

| Member | Type | Description |
|--------|------|-------------|
| `OnDisconnected(callback)` | void | Arm the reconnect callback on a disconnect |
| `OnConnected()` | void | Clear backoff state on a successful reconnect |
| `Tick()` | void | Advance the backoff timer (called on each reconnect failure) |
| `Reset()` | void | Clear attempt/elapsed state (on clean stop) |
| `IsReconnecting()` | bool | Whether in the reconnect window |
| `CurrentAttempt()` | uint32 | Attempt counter |
| `NextIntervalMs()` | uint32 | Backoff interval (exponential to max) |
| buffer_duration_s | uint32 | Reconnect window before giving up (default 30) |

Constructor: `ReconnectHandler(max_interval_s, buffer_duration_s)`.

**Behavior**: on a backend `kDisconnected` status the handler starts the
backoff window; `StreamImpl::OnBackendStatusChange` drives the reconnect
attempts. There is no ring-buffer recording of media during disconnects in the
current implementation.

---

## Comments / Notes

- The in-repo WHIP test server entity (previously `src/test_server/whip_test_server.*`)
  has been **removed**; end-to-end verification now uses MediaMTX as the WHIP
  endpoint (see `specs/008-stream-interface/webrtc-backend-context.md` and
  `stream/README.md`).
- The stream transport stack is libdatachannel (PeerConnection) + cpp_network
  (WHIP HTTP client); OpenSSL/libcurl are not maintained by the stream module
  (reused from cpp_network's TLS bundle).