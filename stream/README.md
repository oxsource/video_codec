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

### WHIP Push (encode + push)

```bash
# Push SMPTE color bars + 1 kHz tone for 30 seconds (no file recording):
bazel run //src/examples:encode_and_push -- --no-record http://localhost:8080/whip 30

# Push and record to local file:
bazel run //src/examples:encode_and_push -- out/out.mp4 http://localhost:8080/whip 5
```

### WHEP Subscribe (GStreamer)

```bash
# Requires GStreamer with webrtc plugin (gst-plugins-rs).
# Subscribe to the WHIP push stream via WHEP endpoint:
gst-launch-1.0 \
  whepclientsrc signaller::whep-endpoint="http://localhost:8080/whep" \
  ! decodebin ! autovideosink
```

The test server supports both WHIP (push) and WHEP (subscribe) protocols on the same port. Push a stream with the WHIP example, then subscribe with any WHEP-compatible client (browser, GStreamer, etc.).

### Media File Mode (no WHIP push required)

The server can broadcast a pre-recorded H.264 file in a loop to all WHEP subscribers:

```bash
# Generate a 60-second SMPTE color bars test file:
ffmpeg -y -f lavfi -i "smptebars=size=640x480:rate=30:duration=60" \
  -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p -g 30 -an \
  /tmp/test_webrtc_60s.h264

# Start server in media mode:
bazel run //src/test_server:whip_test_server -- --port 8080 --media /tmp/test_webrtc_60s.h264

# Play once (no loop):
bazel run //src/test_server:whip_test_server -- --port 8080 --media /tmp/test_webrtc_60s.h264 --no-loop
```

### RTSP Loop Test (FFmpeg)

For comparison, the same H.264 file can be served via RTSP to verify the file itself is valid:

```bash
# Requires mediamtx or rtsp-simple-server running on port 8554.
ffmpeg -re -stream_loop -1 -f h264 -i /tmp/test_webrtc_60s.h264 \
  -c:v copy -f rtsp rtsp://localhost:8554/test
```

## License

MIT