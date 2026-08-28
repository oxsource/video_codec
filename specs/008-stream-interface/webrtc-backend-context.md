# Stream Interface — WebRTC Backend Context

## Current State

The stream library implements a full WebRTC/WHIP publishing backend based on
**libdatachannel** (not libwebrtc). WHIP HTTP signaling uses the **cpp_network**
http client (a local repository dependency) instead of stream-owned libcurl.
The backend creates a real `rtc::PeerConnection`, sends a real offer via WHIP
POST, applies the answer, packetizes H.264/RTCP into RTP, and is verified
end-to-end against a MediaMTX WHIP endpoint with browser subscription.

OpenSSL for libdatachannel's DTLS is reused from cpp_network's source-built TLS
bundle (`@cpp_network//third_party/openssl/{host,android}:openssl`) — the stream
module no longer builds or maintains OpenSSL/libcurl.

## Build Status

```bash
cd stream
bazel build //...                            # ✅ All targets build
bash scripts/verify/host_verify.sh           # ✅ build //... + encode_and_push
bash scripts/verify/android_build.sh         # ✅ core + mock cross-build (android_arm64)

# Server-side prep (MediaMTX), then push and subscribe in a browser at :8889:
/opt/homebrew/opt/mediamtx/bin/mediamtx /opt/homebrew/etc/mediamtx/mediamtx.yml
bazel run //src/examples:encode_and_push -- --config src/examples/stream_conf.json
```

## What Works

| Component | Status |
|-----------|--------|
| Stream interface (`api/`) | ✅ Complete (`StreamConfig`, `StreamStatus`, `StreamBackend`) |
| NetworkConfig (`api/network_config.h`) | ✅ Nested in `StreamConfig`; timeouts, proxy, bind, TLS/CA/mTLS/SNI |
| Stream implementation (`core/`) | ✅ Lifecycle, factory, backend registry |
| Mock backend | ✅ Implemented (`backend/mock/`) — no automated tests (stream has no `tests/` tree) |
| WebRTC backend (`backend/webrtc/`) | ✅ Real `rtc::PeerConnection` (libdatachannel), H.264 RTP packetizer, `send()` |
| WHIP signaling | ✅ `WhipSession` over cpp_network http client (POST offer, PATCH ICE, DELETE; Location→session id) |
| ICE | ✅ STUN `stun:stun.l.google.com:19302`; full offer gathered before POST |
| ABR controller | ✅ Implemented (`core/abr_controller.*`) — no automated tests |
| Reconnect handler | ✅ Implemented (`core/reconnect_handler.*`) — no automated tests |
| Unified JSON config | ✅ `StreamConfig::LoadFromFile`/`::ParseFromJson` in `core/stream_config.cc`; sample `src/examples/stream_conf.json` |
| Log tagging | ✅ `LOG_TAG` per-file mechanism in codec `log_slot.h`; tagged by `webrtc_backend`/`whip_session`/`encode_and_push` |
| Example (encode + push) | ✅ MediaMTX verified, browser subscription |
| `VIDEO_STREAM_REGISTER` macro | ✅ Self-registration pattern |
| `alwayslink = True` | ✅ Static initializer preserved |

## Key Design Decisions

### 1. HTTP stack: cpp_network (local repository)

`stream/WORKSPACE` adds cpp_network via `local_repository("../../cpp_network")`
and bootstraps its deps with `cpp_network_setup()`, so `@curl`/`@openssl` pin
definitions are single-sourced in `cpp_network/cpp_network_deps.bzl`. The stream
module lists no libcurl dependency; `whip_session.cc` builds a synchronous
`cpp_network::http::Client` from `NetworkConfig` (`Options` + `Tls`).

### 2. TLS: reuse cpp_network's source-built OpenSSL

libdatachannel links `@cpp_network//third_party/openssl/{host,android}:openssl`
(selected per platform). This replaced stream's own OpenSSL `configure_make`
build, whose macOS arm64 archive was missing `armcap.o` and failed the
libdatachannel dylib link with `_OPENSSL_armcap_P`.

### 3. Media vs libwebrtc

libwebrtc was planned but replaced by libdatachannel (BSD-style, C++17, ships a
Bazel-friendly CMake build). `src/test_server/whip_test_server.*` was removed;
verification now uses MediaMTX as the WHIP endpoint.

## Key Files

| File | Purpose |
|------|---------|
| `stream/src/api/stream_config.h` | StreamConfig (`::ParseFromJson`/`::LoadFromFile`, embeds NetworkConfig) |
| `stream/src/api/network_config.h` | NetworkConfig: timeouts, TLS, proxy, bind |
| `stream/src/backend/webrtc/webrtc_backend.h/.cc` | WebRTC backend (libdatachannel + whip) |
| `stream/src/backend/webrtc/whip_session.h/.cc` | WHIP signaling over cpp_network http |
| `stream/src/backend/webrtc/webrtc_raii.h` | PeerConnection RAII helpers |
| `stream/src/backend/webrtc/BUILD.bazel` | Build target (`@cpp_network//:cpp_network`, `alwayslink`) |
| `stream/third_party/libdatachannel/BUILD.bazel` | CMake build, OpenSSL from cpp_network TLS bundle |
| `stream/video_stream_deps.bzl` | stream-owned deps (no curl/openssl/libwebrtc) |
| `stream/WORKSPACE` | `local_repository` cpp_network + `cpp_network_setup()` |
| `stream/src/core/stream_config.cc` | Unified JSON schema parser (nlohmann/json) |
| `stream/src/examples/encode_and_push.cc` | Example: encode + record + push (JSON config, `--config`) |
| `stream/src/examples/stream_conf.json` | Sample unified JSON config |
| `stream/README.md` | MediaMTX quickstart |

## Unresolved Issues

1. `[mp4 @ ...] track 1: codec frame size is not set` — muxer warning, doesn't affect streaming.
2. WHIP PATCH (trickle ICE) and DELETE are implemented in `WhipSession` but not yet driven by `webrtc_backend` (it posts a fully gathered offer and closes the peer connection on disconnect); ICE candidate trickling can be wired when low-latency start is needed.
3. No automated WebRTC integration test yet — validation relies on MediaMTX (manual/browser). A deviceless WHIP endpoint for CI is a follow-up.