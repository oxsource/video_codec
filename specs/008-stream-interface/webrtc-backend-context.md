# Stream Interface — WebRTC Backend Context

## Current State

Stream library core, mock backend, WHIP test server, ABR, reconnect, config validation, and example are all implemented and building. The WHIP HTTP signaling handshake works (client POSTs to server, server responds), but **no real RTCPeerConnection** is created on either side — no media flows.

## Build Status

```bash
cd stream
bazel build //src/...          # ✅ All targets build
bazel run //src/test_server:whip_test_server -- --port=8080  # ✅ Server starts, serves player.html
bazel run //src/examples:encode_and_push -- out.mp4 http://localhost:8080/whip 5  # ✅ WHIP handshake succeeds, no media
```

## What Works

| Component | Status |
|-----------|--------|
| Stream interface (`api/`) | ✅ Complete |
| Stream implementation (`core/`) | ✅ Lifecycle, factory, backend registry |
| Mock backend | ✅ Tests pass |
| WHIP test server (HTTP) | ✅ POST/PATCH/DELETE, player.html |
| ABR controller | ✅ Tests pass |
| Reconnect handler | ✅ Tests pass |
| Example (encode + push) | ✅ Builds, WHIP handshake works |
| `VIDEO_STREAM_REGISTER` macro | ✅ Self-registration pattern |
| `alwayslink = True` | ✅ Static initializer preserved |

## What Needs Work (Tomorrow)

### 1. libwebrtc Dependency

`stream/video_stream_deps.bzl` has `_libwebrtc()` using `http_archive` with a placeholder URL:

```python
http_archive(
    name = "libwebrtc",
    urls = ["https://github.com/webrtc-sdk/libwebrtc/archive/refs/tags/m127.tar.gz"],
    sha256 = "d5558cd419c8d46bdc958064cb97f963d1ea793866414c025906ec15033512ed",
    strip_prefix = "libwebrtc-m127",
    build_file = "//third_party/libwebrtc:BUILD.bazel",
)
```

`stream/third_party/libwebrtc/BUILD.bazel` uses `rules_foreign_cc` + `cmake`. This needs to be replaced with a real libwebrtc build that actually compiles.

**Options**:
- Use a pre-built libwebrtc SDK (e.g., from webrtc-build or a system package)
- Or build from source using Google's `gn`/`ninja` (not cmake — libwebrtc doesn't use cmake)
- Or use `rules_foreign_cc` with a custom configure script

### 2. WebRTC Backend (`webrtc_backend.cc`)

Currently creates a fake SDP offer and sends it via WHIP HTTP. Needs to:

```cpp
// Pseudocode for real implementation:
class WebrtcBackend : public StreamBackend {
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
  std::unique_ptr<WhipSession> whip_;

  video::codec::Status Connect(const StreamConfig& config) override {
    // 1. Create PeerConnectionFactory
    // 2. Create PeerConnection with ICE config
    // 3. Create video/audio tracks
    // 4. Create offer SDP via CreateOffer()
    // 5. Send offer via WHIP POST
    // 6. Set remote description from WHIP answer
    // 7. Exchange ICE candidates via WHIP PATCH
  }

  video::codec::Status SendVideo(const video::codec::VideoPacket& packet) override {
    // Convert VideoPacket to webrtc::VideoFrame
    // Push to webrtc::VideoTrackSource
  }
};
```

### 3. WHIP Test Server (`whip_test_server.cc`)

Currently returns a fake SDP answer. Needs to:

```cpp
// Pseudocode for real WHIP server:
class WhipTestServer {
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  std::unordered_map<std::string, rtc::scoped_refptr<webrtc::PeerConnectionInterface>> sessions_;

  std::string HandleWhipPost(const std::string& body) override {
    // 1. Parse offer SDP from body
    // 2. Create PeerConnection on server side
    // 3. Set remote description from offer
    // 4. Create answer SDP via CreateAnswer()
    // 5. Return answer SDP in 201 response
    // 6. Add video track to player for browser consumption
  }
};
```

### 4. `webrtc_raii.h`

Already has RAII wrappers for `PeerConnectionFactoryInterface` and `PeerConnectionInterface`. May need to extend with `VideoTrackInterface`, `AudioTrackInterface`, etc.

### 5. BUILD.bazel for webrtc_backend

Already has `alwayslink = True`. Needs to add `@libwebrtc` to deps when the library is ready:

```python
cc_library(
    name = "webrtc_backend",
    ...
    deps = [
        "//src/api:stream_api",
        "//src/core:stream_core",
        "@libwebrtc//:webrtc",  # ← needs real libwebrtc
    ],
)
```

## Key Files

| File | Purpose |
|------|---------|
| `stream/src/backend/webrtc/webrtc_backend.h/.cc` | WebRTC backend implementation |
| `stream/src/backend/webrtc/whip_session.h/.cc` | WHIP HTTP signaling client |
| `stream/src/backend/webrtc/webrtc_raii.h` | RAII wrappers for WebRTC types |
| `stream/src/backend/webrtc/BUILD.bazel` | Build target (alwayslink) |
| `stream/src/test_server/whip_test_server.h/.cc` | WHIP test server |
| `stream/src/test_server/web/player.html` | Browser player page |
| `stream/video_stream_deps.bzl` | libwebrtc dependency placeholder |
| `stream/third_party/libwebrtc/BUILD.bazel` | libwebrtc cmake build template |
| `stream/src/examples/encode_and_push.cc` | Example: encode + record + push |
| `stream/src/examples/BUILD.bazel` | Example build target |

## Unresolved Issues

1. `[mp4 @ ...] track 1: codec frame size is not set` — muxer warning, doesn't affect streaming
2. libwebrtc dependency needs real build setup (gn/ninja or pre-built SDK)
3. Both client and server need real `PeerConnectionFactory` creation