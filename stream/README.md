# video_stream

A C++ library for pushing encoded media streams with pluggable transport backends. Peer module to [video_codec](https://github.com/anomalyco/video_codec).

## Architecture

```
stream/                     # Independent Bazel workspace
├── src/
│   ├── api/                # Public interfaces (Stream, StreamBackend, StreamConfig, StreamStatus)
│   ├── core/               # Implementation (StreamImpl, ABR controller, reconnect handler, backend registry)
│   ├── backend/
│   │   ├── mock/           # Mock backend for unit testing
│   │   └── webrtc/         # WebRTC/WHIP backend (v1)
│   └── test_server/        # Standalone WHIP test server for validation
├── tests/                  # Google Test based unit tests
└── mk/                     # Make module system (AOSP-style)
```

## Building

```bash
cd stream
bazel build //src/api:stream_api
bazel build //src/test_server:whip_test_server
bazel test //tests/...
```

## Quickstart

```cpp
#include "video/stream/stream.h"

using namespace video::stream;

StreamConfig config;
config.backend_type = "webrtc";
config.remote_url = "http://localhost:8080/whip/endpoint";
config.video_codec = "h264";
config.initial_bitrate_kbps = 2000;

auto stream = Stream::Create(config);
stream->Init();
stream->Start();
stream->SendVideo(video_packet);
stream->SendAudio(audio_packet);
stream->Stop();
stream->Release();
```

## Test Server

```bash
bazel run //src/test_server:whip_test_server -- --port=8080
# Open http://localhost:8080 in a browser
```

## License

MIT