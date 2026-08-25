# Stream Interface Contract

## Overview

The stream module exposes a unified interface for pushing encoded media streams. The design follows the same pattern as the codec module: abstract interface + backend plugin architecture.

## Namespace

```
video::stream
```

## Stream Interface

```cpp
// stream.h — Unified stream interface

namespace video::stream {

class Stream {
 public:
  // Factory: create a stream instance with the given backend
  static std::unique_ptr<Stream> Create(const StreamConfig& config);

  virtual ~Stream() = default;

  // Lifecycle
  virtual Status Init() = 0;
  virtual Status Start() = 0;
  virtual Status Stop() = 0;
  virtual void Release() = 0;

  // Media input (consume encoded media from codec module)
  virtual Status SendVideo(const VideoPacket& packet) = 0;
  virtual Status SendAudio(const AudioPacket& packet) = 0;

  // Configuration
  virtual Status UpdateConfig(const StreamConfig& config) = 0;

  // Observability
  virtual StreamStatus GetStatus() const = 0;
  virtual void SetStatusCallback(StatusCallback callback) = 0;

  // Not copyable or movable
  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;
};

}  // namespace video::stream
```

## StreamBackend Interface

```cpp
// stream_backend.h — Contract each transport backend implements

namespace video::stream {

class StreamBackend {
 public:
  virtual ~StreamBackend() = default;

  virtual Status Connect(const StreamConfig& config) = 0;
  virtual Status SendVideo(const VideoPacket& packet) = 0;
  virtual Status SendAudio(const AudioPacket& packet) = 0;
  virtual Status Disconnect() = 0;

  virtual StreamStats GetStats() = 0;
  virtual void SetStatusCallback(StatusCallback callback) = 0;

  StreamBackend(const StreamBackend&) = delete;
  StreamBackend& operator=(const StreamBackend&) = delete;
};

// Factory function for backend registration
using BackendFactory = std::unique_ptr<StreamBackend>(*)(const StreamConfig&);
void RegisterBackend(const std::string& name, BackendFactory factory);

}  // namespace video::stream
```

## StreamConfig

```cpp
// stream_config.h

namespace video::stream {

struct StreamConfig {
  std::string backend_type;          // e.g., "webrtc"
  std::string remote_url;            // WHIP endpoint URL
  std::string video_codec;           // "h264"
  std::string audio_codec;           // "opus" or "aac"

  uint32_t initial_bitrate_kbps = 2000;
  uint32_t max_bitrate_kbps = 5000;
  uint32_t min_bitrate_kbps = 200;

  uint32_t resolution_width = 1280;
  uint32_t resolution_height = 720;
  uint32_t framerate = 30;

  uint32_t buffer_duration_s = 30;
  uint32_t reconnect_max_interval_s = 30;

  std::string stun_server;
  std::string turn_server;
};

}  // namespace video::stream
```

## StreamStatus

```cpp
// stream_status.h

namespace video::stream {

enum class StreamState {
  kCreated,
  kConfigured,
  kConnecting,
  kStreaming,
  kReconnecting,
  kDisconnected,
  kDestroyed
};

struct StreamStatus {
  StreamState state = StreamState::kCreated;
  uint32_t bitrate_kbps = 0;
  uint32_t rtt_ms = 0;
  float packet_loss_pct = 0.0f;
  uint64_t bytes_sent = 0;
  uint64_t frames_sent = 0;
  uint64_t frames_dropped = 0;
  uint64_t uptime_s = 0;
  std::string last_error;
};

using StatusCallback = std::function<void(const StreamStatus&)>;

}  // namespace video::stream
```

## Status/Result Types

Reuse `video::codec::Status` and `video::codec::Result<T>` from the codec module's core types to maintain consistency.

## Backend Registration Pattern

Follow the same self-registration pattern as the codec module (`VIDEO_CODEC_REGISTER` macro):

```cpp
// In webrtc_backend.cc:
namespace {
  auto registered = [] {
    video::stream::RegisterBackend("webrtc", WebrtcBackend::Create);
    return true;
  }();
}
```

## Thread Safety

- `Stream` is NOT thread-safe. All calls must be made from the same thread.
- `StatusCallback` may be invoked from an internal transport thread.
- ABR controller runs on the stream's thread, sampling stats from the backend.

## Error Handling

- All fallible operations return `Status` (no exceptions).
- Error codes match the codec module's `StatusCode` enum.
- `Start()` after an unrecoverable error returns `Status::kInvalidState`.