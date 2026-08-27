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
│   └── examples/           # Runnable examples
└── mk/                     # Make module system (AOSP-style)
```

## Building

```bash
cd stream
bazel build //src/api:stream_api
bazel build //src/examples:encode_and_push
```

## Quickstart

### Programmatic

```cpp
#include "video/stream/stream.h"

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
`//src/core:stream_core`, implemented behind the `StreamConfig::LoadFromFile` /
`::ParseFromJson` static methods). Callers only hand over the content; missing
keys fall back to module defaults, and the WHIP URL is derived from
`signal.host + "/" + signal.path + "/whip"`.

```cpp
#include "src/api/stream_config.h"

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
# (src/examples/stream_conf.json -> http://localhost:8889/test/whip),
# skipping the local MP4 recording:
bazel run //src/examples:encode_and_push -- --config src/examples/stream_conf.json

# Push and record to a local file for a fixed duration:
bazel run //src/examples:encode_and_push -- --config src/examples/stream_conf.json \
  --no-record=false --seconds 5
```

Browser preview: open `http://localhost:8889`, select the stream, and play it (MediaMTX WebRTC/WHIP subscription).

## License

MIT