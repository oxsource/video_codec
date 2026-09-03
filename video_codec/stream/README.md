# video_stream

A C++ library for pushing encoded media streams with pluggable transport backends. Part of the [video_codec](https://github.com/oxsource/video_codec) monorepo.

## Architecture

```
stream/                     # Package //stream/... in the merged workspace
├── src/
│   ├── api/                # Public interfaces (Stream, StreamBackend, StreamConfig, StreamStatus)
│   ├── core/               # Implementation (StreamImpl, ABR controller, reconnect handler, backend registry)
│   ├── backend/
│   │   ├── mock/           # Mock backend for unit testing
│   │   └── webrtc/         # WebRTC/WHIP backend (v1)
│   └── examples/           # Runnable examples
└── third_party/            # Build wrappers (libdatachannel, nlohmann_json)
```

## Building

From the repo root (single workspace):

```bash
bazel build //stream/src/api:stream_api
bazel build //stream/src/examples:encode_and_push
```

Or use the make targets:

```bash
make build-stream
make build-example
```

## Quickstart

### Programmatic

```cpp
#include "stream/src/api/stream.h"
#include "stream/src/api/stream_config.h"

using namespace video::stream;

StreamConfig config;
config.backend_type = "webrtc";
config.remote_url = "http://localhost:8889/test/whip";
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

### From a unified JSON config

The stream module owns the JSON schema (field keys + all defaults in
`//stream/src/core:stream_core`, implemented behind the `StreamConfig::LoadFromFile` /
`::ParseFromJson` static methods). Callers only hand over the content; missing
keys fall back to module defaults, and the WHIP URL is derived from
`signal.host + "/" + signal.path + "/whip"`.

```cpp
#include "stream/src/api/stream_config.h"

auto res = StreamConfig::LoadFromFile("config.json");
// res.ok(), res.value() or res.Release() -> fully-resolved StreamConfig
```

## Testing with MediaMTX

Requires [MediaMTX](https://github.com/bluenviron/mediamtx) running on port 8889 (default WebRTC/WHIP port). Start the server before pushing with `encode_and_push`:

```bash
# Start MediaMTX with an explicit config (Homebrew install). The Web UI at
# http://localhost:8889 lets you subscribe to the pushed stream directly in a
# browser:
/opt/homebrew/opt/mediamtx/bin/mediamtx /opt/homebrew/etc/mediamtx/mediamtx.yml

# Push SMPTE color bars using the sample JSON config
# (stream/src/examples/stream_conf.json -> http://localhost:8889/test/whip),
# pushing only (no local file):
bazel run //stream/src/examples:encode_and_push -- --config stream/src/examples/stream_conf.json

# Push and record to a local file for a fixed duration:
bazel run //stream/src/examples:encode_and_push -- --config stream/src/examples/stream_conf.json \
  --record --seconds 5
```

Browser preview: open `http://localhost:8889`, select the stream, and play it (MediaMTX WebRTC/WHIP subscription).

## License

MIT
