# Quickstart: Encoder-to-Queue Push Wiring (spec 004)

**Branch**: `004-encoder-queue-wiring` | **Date**: 2026-08-12
**Feature**: [spec.md](spec.md)

Short guide for wiring a real encoder to the output queue so encoded packets flow into the
queue automatically (push mode), instead of the caller collecting them from the pull API.

## 1. Pull mode (default — unchanged)

```cpp
#include <video_codec/video_codec.h>

auto encoder = video_codec::CreateVideo(cfg);
encoder->Init();
video_codec::Result<video_codec::Packet> r = encoder->Encode(frame);
// r.value() carries the full packet; nothing is pushed anywhere.
```

## 2. Push mode (encoder → queue)

```cpp
#include <video_codec/video_codec.h>
#include "queue/packet_queue.h"
#include "consumer/file_sink_consumer.h"

video_codec::PacketQueue queue(64, video_codec::Backpressure::kBlock);

auto encoder = video_codec::CreateVideo(cfg);
if (encoder->Init() != video_codec::Status::kOk) return 1;
if (encoder->SetOutputSink(&queue) != video_codec::Status::kOk) return 1;

// Drain on a consumer thread.
video_codec::FileSinkConsumer sink("out.h264");
std::thread pump([&] { queue.Await(*sink); });

for (int i = 0; i < N; ++i) {
  // In push mode the packet goes into the queue; the returned packet is empty.
  (void)encoder->Encode(frame_i);
}
encoder->Flush();                 // pushes any final packet, then queue.Flush()
queue.MarkEos();                  // CALLER marks EOS (all producers done)
pump.join();                      // consumer sees kEos and stops; file is complete
```

- Only the encoder side changes in this feature: `queue`, `PacketSource::Await`,
  `FileSinkConsumer` already exist and are used as-is.
- Back-pressure is the queue's job (`kBlock` default): a slow consumer naturally slows the
  encoder, zero loss.

## 3. Audio

Same pattern with `CreateAudio` / `AudioFrame`; audio packets are pushed via
`PacketSink::Push(VideoPacket&&) / Push(AudioPacket&&)`.

## 4. Rules to remember

- `SetOutputSink(nullptr)` detaches → back to pull mode.
- Attach before `Encode()`; attach is non-owning (caller keeps the queue alive).
- The caller marks end-of-stream (`MarkEos()`), not the encoder — video and audio may share
  one queue.

## 5. Building & testing

From the `codec/` workspace root:

```bash
bazel build //src/framework/...
bazel test //tests/backend/ffmpeg/...   # push-mode integration tests
bazel test //tests/...                   # full suite (all existing tests still pass)
```
