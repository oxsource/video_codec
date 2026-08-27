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

```cpp
#include "video/stream/stream.h"

using namespace video::stream;

StreamConfig config;
config.backend_type = "webrtc";
config.remote_url = "http://localhost:8889/whip";
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

## Testing with MediaMTX

Requires [MediaMTX](https://github.com/bluenviron/mediamtx) running on port 8889 (default WebRTC/WHIP port). Start the server before pushing with `encode_and_push`:

```bash
# Start MediaMTX with an explicit config (Homebrew install). The Web UI at
# http://localhost:8889 lets you subscribe to the pushed stream directly in a
# browser:
/opt/homebrew/opt/mediamtx/bin/mediamtx /opt/homebrew/etc/mediamtx/mediamtx.yml

# Generate a 60-second SMPTE color bars test file:
ffmpeg -y -f lavfi -i "smptebars=size=640x480:rate=30:duration=60" \
  -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p -g 30 -an \
  /tmp/test_webrtc_60s.h264

# Push SMPTE color bars for 30 seconds (no file recording):
bazel run //src/examples:encode_and_push -- --no-record --url http://localhost:8889/whip --seconds 30

# Push and record to local file:
bazel run //src/examples:encode_and_push -- --output out.mp4 --url http://localhost:8889/whip --seconds 5
```

Browser preview: open `http://localhost:8889`, select the stream, and play it (MediaMTX WebRTC/WHIP subscription).

## License

MIT