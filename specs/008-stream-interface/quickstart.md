# Quickstart: Stream Interface

## Prerequisites

- Bazel 6.5.0 (matching codec workspace)
- C++17 compiler
- Existing codec module built (stream depends on `video_codec` public API)
- cpp_network local repository available at `../../cpp_network` (WHIP signaling / TLS stack)

## Building

```bash
# Build the stream library (within stream/ workspace)
cd stream
bazel build //src/api:stream_api        # public API (Stream, StreamConfig, StreamStatus)
bazel build //src/core:stream_core      # implementation (StreamImpl, ABR, reconnect, JSON config)
bazel build //src/backend/webrtc:webrtc_backend
bazel build //src/examples:encode_and_push
bazel build //...                       # everything

# Host + Android validation (mirrors codec's mk/verify modules)
make host-build        # bazel build //...
make host-verify       # build library + example
make android-build     # Android arm64 cross-build (needs NDK)
```

## Basic Usage

```cpp
#include "src/api/stream.h"
#include "src/api/stream_config.h"

using namespace video::stream;

// 1. Configure stream
StreamConfig config;
config.backend_type = "webrtc";
config.remote_url = "http://localhost:8889/test/whip";
config.video_codec = "h264";
config.audio_codec = "aac";
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

## Configuration from JSON

The stream module owns the JSON schema (field keys + defaults in
`src/core/stream_config.cc`); callers only supply content. Missing keys fall
back to module defaults, and the WHIP URL is derived from the signal
`host` + `path` as `host + "/" + path + "/whip"`.

```cpp
#include "src/api/stream_config.h"

auto res = StreamConfig::LoadFromFile("config.json");
// res.ok(); auto config = res.Release(); (fully-resolved StreamConfig)
```

Sample config: `src/examples/stream_conf.json`.

## End-to-End Verification with MediaMTX

The old in-repo test server (`src/test_server/whip_test_server.*`) was removed;
verification now uses [MediaMTX](https://github.com/bluenviron/mediamtx) as the
WHIP endpoint, which also lets you subscribe to the stream in a browser.

```bash
# 1. Start MediaMTX (Homebrew) — Web UI at http://localhost:8889
/opt/homebrew/opt/mediamtx/bin/mediamtx /opt/homebrew/etc/mediamtx/mediamtx.yml

# 2. Push SMPTE color bars to http://localhost:8889/test/whip
bazel run //src/examples:encode_and_push -- --config src/examples/stream_conf.json

# 3. Open http://localhost:8889 in a browser and select the stream
```

## Make Targets

```bash
make help            # List all targets
make host-build      # Host: bazel build //...
make host-verify     # Host: build stream library + example
make build-example   # Build encode_and_push
make android-build   # Android arm64 cross-build (stream_core + mock_backend)
make android-verify  # Full android validation (= android-build)
make clean-out       # Remove generated output under out/
```