# Contract: Output Queue / Transport

**Owner**: `src/framework/queue` (ring buffer) + `src/framework/consumer` (consumers) |
**Frozen by**: `codec/doc/architecture/output-queue.md`, `data-model.md` §8, ADR-005.

This contract freezes the transport between encoder output and a consumer. It is
**transport-first**: the encoder and the consumer never reference each other directly —
only the `OutputSink` / `PacketSource` / `PacketConsumer` interfaces. A file sink
and a network streamer both implement `PacketConsumer` and are therefore
interchangeable.

## Enums

```cpp
enum class Backpressure { kBlock, kDropOldest, kError };
// PacketSource::PopResult is a nested enum of PacketSource (see below).
```

## Producer endpoint — `OutputSink`

Implemented by the ring buffer's producer side. The encoder writes here.

```cpp
class OutputSink {
  public:
    virtual ~OutputSink() = default;
    // Unified packet; the packet's PacketType (kVideo / kAudio) routes it to
    // the matching internal ring.
    virtual Status Submit(Packet&& pkt) = 0;
    virtual Status Flush() { return Status::kOk; }  // segment boundary
};
```

- `Submit` **moves** ownership of the packet into the queue.
- On a full queue, behavior follows the queue's `Backpressure` policy
  (`kBlock` waits, `kDropOldest` overwrites, `kError` returns a back-pressure code).
- `Flush()` signals "end of current segment" — distinct from stream end.

## Consumer endpoint — `PacketSource`

Implemented by the ring buffer's consumer side. The drain loop reads here. Also
defines `PacketSink` — the dispatch target `Await` delivers to (defined in
`queue`, implemented by `consumer::PacketConsumer`).

```cpp
class PacketSink {
  public:
    virtual ~PacketSink() = default;
    virtual Status Consume(Packet&& pkt) = 0;
    virtual Status Finish() { return Status::kOk; }  // EOS / teardown
};

class PacketSource {
  public:
    // Result of Pop(): kOk (packet in `out`), kEmpty (timeout/empty), kEos
    // (end-of-stream and drained).
    enum class PopResult { kOk, kEmpty, kEos };

    virtual ~PacketSource() = default;
    virtual PopResult Pop(Packet& out, int64_t deadline_us) = 0;  // blocking

    // Await mechanism (replaces the former PacketSource::Await): blocks on the calling
    // thread, delivering every packet to `sink` until EOS (then sink.Finish());
    // a failing Consume stops the loop.
    virtual Status Await(PacketSink& sink, int64_t deadline_us = 100'000) = 0;

    virtual void MarkEos() = 0;   // encoder signals end of stream
};
```

- `Pop(deadline_us)` blocks up to `deadline_us` (negative/0 = non-blocking
  `TryPop` semantics); returns `kEmpty` on timeout, `kEos` after `MarkEos()` and drained.
- The consumer never allocates; packets are moved out of slots.
- `Await(sink)` runs on the consumer thread; the caller just spawns a thread and
  calls `source.Await(*consumer)` — no separate pump class.

## Ring buffer — `PacketQueue`

```cpp
class PacketQueue : public OutputSink, public PacketSource {
  public:
    // capacity MUST be > 0 and a power of two (index masking).
    PacketQueue(size_t capacity, Backpressure policy = Backpressure::kBlock);
    // OutputSink
    Status Submit(Packet&&) override;   // routed to video/audio ring by type
    // PacketSource
    PacketSource::PopResult Pop(Packet&, int64_t) override;  // round-robin across both rings
    void MarkEos() override;
};
```

- SPSC: one producer (encoder), one consumer (drain loop).
- Holds fixed `slots[]` of the unified `Packet` on **two independent rings** (one per
  media, `PacketType`-routed); packets moved in/out. Back-pressure is per-media.

## Consumer interface — `PacketConsumer`

Implemented by file sink and streamer. The drain loop (`PacketSource::Await`) calls this.

```cpp
class PacketConsumer {
  public:
    virtual ~PacketConsumer() = default;
    // One Consume for both media; PacketType tells the consumer which media.
    // Media-specific consumers return kUnsupportedOperation for the other.
    virtual Status Consume(Packet&& pkt) = 0;
    virtual Status Flush() { return Status::kOk; }
    virtual Status Finish() { return Status::kOk; } // EOS / teardown
};
```

## Drain loop — `PacketSource::Await`

Not an interface; a reusable helper that bridges source → consumer on the consumer
thread (see `output-queue.md` §3). Calls `Consume` for each popped packet; `Finish()` at
EOS; propagates `Consume` failures to logging/back-off.

## Concrete consumer: `FileSinkConsumer` (implement later)

Implements `PacketConsumer`. Contract obligations:
1. Preserve packet order; write `keyframe` packets (and SPS/PPS) at segment starts.
2. Raw bitstream: write Annex-B to `.h264`/`.aac`; **or** feed a muxer for
   `.mp4`/`.mkv` (muxing deferred — muxer is itself a `PacketConsumer`).
3. On `Finish()`: flush and close the file.
4. Must not block the pump indefinitely; file I/O errors return a `Status`.

## Concrete consumer: `StreamConsumer` (推流, implement later)

Implements `PacketConsumer`. Contract obligations:
1. **Framing**: chunk Annex-B into protocol units (FLV tags for RTMP, RTP for
   SRT/WebRTC); the encoder stays protocol-agnostic.
2. **Connection lifecycle**: connect on first `Consume` (or `Flush`), reconnect on drop,
   teardown on `Finish()`.
3. **Pacing**: send at `pts_us` cadence; never dump the whole backlog at once.
4. **Back-pressure**: a slow/blocked socket makes `Consume` slow → pump slows → ring
   fills → encoder slows. `Consume` must NOT swallow errors and spin; return the error.
5. **Audio+video**: either one `StreamConsumer` muxes both, or two are fed by a muxing
   pump. The contract is per-stream; combining is a wiring concern.

## Acceptance

A transport is contract-complete when:
- encoder pushes via `OutputSink` and an independent `PacketSource::Await`+`PacketConsumer`
  drains it with no packet loss (under `kBlock`) and correct order, AND
- swapping `FileSinkConsumer` for `StreamConsumer` requires no encoder change.
