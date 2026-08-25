# Quickstart: Stream Interface

## Prerequisites

- Bazel 6.5.0 (matching codec workspace)
- C++17 compiler
- Existing codec module built (stream depends on `video_codec` public API)

## Building

```bash
# Build stream library (within stream/ workspace)
cd stream
bazel build //src/stream:stream

# Build test server
bazel build //src/stream/test_server:whip_test_server

# Build tests
bazel test //tests/stream/...
```

## Basic Usage

```cpp
#include "video/stream/stream.h"

using namespace video::stream;

// 1. Configure stream
StreamConfig config;
config.backend_type = "webrtc";
config.remote_url = "http://localhost:8080/whip/endpoint";
config.video_codec = "h264";
config.initial_bitrate_kbps = 2000;

// 2. Create and start
auto stream = Stream::Create(config);
stream->Init();
stream->Start();

// 3. Push encoded media (from codec module)
stream->SendVideo(video_packet);
stream->SendAudio(audio_packet);

// 4. Monitor status
auto status = stream->GetStatus();
printf("Bitrate: %u kbps, RTT: %u ms\n",
       status.bitrate_kbps, status.rtt_ms);

// 5. Stop and cleanup
stream->Stop();
stream->Release();
```

## Running the Test Server

```bash
# Start the WHIP test server on port 8080
bazel run //src/stream/test_server:whip_test_server -- --port=8080

# Open browser at http://localhost:8080 to see the player page
```

## Make Targets

```bash
make stream-build       # Build stream library
make stream-test        # Run stream tests
make stream-server      # Start test server
make stream-verify      # Build + test + server smoke test
```