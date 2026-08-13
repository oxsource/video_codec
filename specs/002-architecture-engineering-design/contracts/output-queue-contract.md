# Contract: Output Queue / Transport

**Owner**: `src/framework/queue` (ring buffer) + `src/framework/consumer` (consumers) |
**Frozen by**: `codec/doc/architecture/output-queue.md`, `data-model.md` §8, ADR-005.

This contract freezes the transport between encoder output and a consumer. It is
**transport-first**: the encoder and the consumer never reference each other directly —
only the `PacketSink` / `PacketSource` interfaces. A file sink
and a network streamer both implement `PacketSink` and are therefore
interchangeable.

## Enums

```cpp
enum class Backpressure { kBlock, kLatest, kError };
// Pull outcomes reuse the global Status: kOk (packet), kEmpty (timeout/empty),
// kEos (end-of-stream and drained).
```

## Packet sink — the one endpoint contract

`PacketSink` is the single destination contract for encoded packets, serving
BOTH transport directions:

- **producer → queue**: the encoder hands packets to the queue via `Push`
  (the ring buffer's producer side implements `PacketSink` for this side);
- **queue → consumer**: `PacketSource::Await` delivers packets to the consumer
  via the same `Push` (`consumer::PacketConsumer` implements `PacketSink`).

```cpp
class PacketSink {
  public:
    virtual ~PacketSink() = default;
    virtual Status Push(VideoPacket&& pkt) = 0;
    virtual Status Push(AudioPacket&& pkt) = 0;
    virtual Status Flush() { return Status::kOk; }   // segment boundary
    virtual Status Finish() { return Status::kOk; }  // EOS / teardown
};
```

- `Push` **moves** ownership of the packet into the sink.
- On a full queue, behavior follows the queue's `Backpressure` policy
  (`kBlock` waits, `kLatest` overwrites, `kError` returns a back-pressure code).
- `Flush()` signals "end of current segment" — distinct from stream end.
- `Finish()` is the EOS teardown (e.g. flush + close the file).

## Consumer endpoint — `PacketSource`

Implemented by the ring buffer's consumer side. The drain loop reads here. `Await`
delivers every packet to a `PacketSink` (the unified contract above, implemented by
`consumer::PacketConsumer`).

```cpp
class PacketSource {
  public:
    virtual ~PacketSource() = default;
    // Returns Status::kOk (packet), Status::kEmpty (timeout/empty), or
    // Status::kEos (after MarkEos() and drained).
    virtual Status Pull(VideoPacket& out, int64_t deadline_us) = 0;  // blocking
    virtual Status Pull(AudioPacket& out, int64_t deadline_us) = 0;

    // Await mechanism (replaces the former PacketPump): blocks on the calling
    // thread, delivering every packet to `sink` until EOS (then sink.Finish());
    // a failing Push stops the loop.
    virtual Status Await(PacketSink& sink, int64_t deadline_us = 100'000) = 0;

    virtual void MarkEos() = 0;   // encoder signals end of stream
};
```

- `Pull(deadline_us)` blocks up to `deadline_us` (negative/0 = non-blocking
  `TryPop` semantics); returns `Status::kEmpty` on timeout, `Status::kEos` after
  `MarkEos()` and drained.
- The consumer never allocates; packets are moved out of slots.
- `Await(sink)` runs on the consumer thread; the caller just spawns a thread and
  calls `source.Await(*consumer)` — no separate pump class.

## Ring buffer — `PacketQueue`

```cpp
class PacketQueue : public PacketSink, public PacketSource {
  public:
    // capacity MUST be > 0 and a power of two (index masking).
    PacketQueue(size_t capacity, Backpressure policy = Backpressure::kBlock);
    // PacketSink (producer side: encoder Pushes into the queue)
    Status Push(VideoPacket&&) override;
    Status Push(AudioPacket&&) override;
    // PacketSource
    Status Pull(VideoPacket&, int64_t) override;
    Status Pull(AudioPacket&, int64_t) override;
    void MarkEos() override;
};
```

- SPSC: one producer (encoder), one consumer (drain loop).
- Holds fixed `slots[]` of `VideoPacket` / `AudioPacket` on **two independent
  rings** (one per media); packets moved in/out. Back-pressure is per-media.

## Consumer interface — `PacketConsumer`

Implemented by file sink and streamer. The drain loop (`PacketSource::Await`) calls this.

```cpp
class PacketConsumer {
  public:
    virtual ~PacketConsumer() = default;
    // Video and audio are distinct types with their own Push overloads;
    // media-specific consumers return kUnsupportedOperation for the other.
    virtual Status Push(VideoPacket&& pkt) = 0;
    virtual Status Push(AudioPacket&& pkt) = 0;
    virtual Status Flush() { return Status::kOk; }
    virtual Status Finish() { return Status::kOk; } // EOS / teardown
};
```

## Drain loop — `PacketSource::Await`

Not an interface; a reusable helper that bridges source → consumer on the consumer
thread (see `output-queue.md` §3). Calls `Push` for each popped packet; `Finish()` at
EOS; propagates `Push` failures to logging/back-off.

## Concrete consumer: `FileSinkConsumer` (implement later)

Implements `PacketConsumer`. Contract obligations:
1. Preserve packet order; write `keyframe` packets (and SPS/PPS) at segment starts.
2. Raw bitstream: write Annex-B to `.h264`/`.aac`.
   MP4 output is NOT a consumer: it is the api `Muxer` interface (peer of
   `VideoEncoder`/`AudioEncoder`), implemented by the backends; a Muxer
   implements `PacketSink` so `PacketSource::Await(*muxer)` hands packets to it
   directly and it writes a fragmented MP4 through an `io::ByteSink`.
3. On `Finish()`: flush and close the file.
4. Must not block the pump indefinitely; file I/O errors return a `Status`.

## Concrete consumer: `StreamConsumer` (推流, implement later)

Implements `PacketConsumer`. Contract obligations:
1. **Framing**: chunk Annex-B into protocol units (FLV tags for RTMP, RTP for
   SRT/WebRTC); the encoder stays protocol-agnostic.
2. **Connection lifecycle**: connect on first `Push` (or `Flush`), reconnect on drop,
   teardown on `Finish()`.
3. **Pacing**: send at `pts_us` cadence; never dump the whole backlog at once.
4. **Back-pressure**: a slow/blocked socket makes `Push` slow → pump slows → ring
   fills → encoder slows. `Push` must NOT swallow errors and spin; return the error.
5. **Audio+video**: either one `StreamConsumer` muxes both, or two are fed by a muxing
   pump. The contract is per-stream; combining is a wiring concern.

## Acceptance

A transport is contract-complete when:
- encoder pushes via `PacketSink` (the queue's producer side) and an independent
  `PacketSource::Await`+`PacketConsumer` drains it with no packet loss (under
  `kBlock`) and correct order, AND
- swapping `FileSinkConsumer` for `StreamConsumer` requires no encoder change.
