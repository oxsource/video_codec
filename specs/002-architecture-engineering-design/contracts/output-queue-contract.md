# Contract: Output Queue / Transport

**Owner**: `src/framework/queue` (ring buffer) + `src/framework/consumer` (consumers) |
**Frozen by**: `codec/doc/architecture/output-queue.md`, `data-model.md` §8, ADR-005.

This contract freezes the transport between encoder output and a consumer. It is
**transport-first**: the encoder and the consumer never reference each other directly —
only the `OutputSink` / `EncodedPacketSource` / `PacketConsumer` interfaces. A file sink
and a network streamer both implement `PacketConsumer` and are therefore
interchangeable.

## Enums

```cpp
enum class Backpressure { kBlock, kDropOldest, kError };

enum class PopResult { kOk, kEmpty, kEos };   // from EncodedPacketSource::Pop
```

## Producer endpoint — `OutputSink`

Implemented by the ring buffer's producer side. The encoder writes here.

```cpp
class OutputSink {
  public:
    virtual ~OutputSink() = default;
    virtual StatusCode Submit(EncodedPacket&& pkt) = 0;     // video
    virtual StatusCode Submit(AudioPacket&& pkt) = 0;       // audio
    virtual StatusCode Flush() { return StatusCode::kOk; }  // segment boundary
};
```

- `Submit` **moves** ownership of the packet into the queue.
- On a full queue, behavior follows the queue's `Backpressure` policy
  (`kBlock` waits, `kDropOldest` overwrites, `kError` returns a back-pressure code).
- `Flush()` signals "end of current segment" — distinct from stream end.

## Consumer endpoint — `EncodedPacketSource`

Implemented by the ring buffer's consumer side. The drain loop reads here.

```cpp
class EncodedPacketSource {
  public:
    virtual ~EncodedPacketSource() = default;
    virtual PopResult Pop(EncodedPacket& out, int64_t deadline_us) = 0;  // blocking
    virtual PopResult Pop(AudioPacket& out, int64_t deadline_us) = 0;
    virtual void MarkEos() = 0;   // encoder signals end of stream
};
```

- `Pop(deadline_us)` blocks up to `deadline_us` (negative/0 = non-blocking `TryPop`
  semantics); returns `kEmpty` on timeout, `kEos` after `MarkEos()` and drained.
- The consumer never allocates; packets are moved out of slots.

## Ring buffer — `EncodedPacketQueue`

```cpp
class EncodedPacketQueue : public OutputSink, public EncodedPacketSource {
  public:
    // capacity MUST be > 0 and a power of two (index masking).
    EncodedPacketQueue(size_t capacity, Backpressure policy = Backpressure::kBlock);
    // OutputSink
    StatusCode Submit(EncodedPacket&&) override;
    StatusCode Submit(AudioPacket&&) override;
    // EncodedPacketSource
    PopResult Pop(EncodedPacket&, int64_t) override;
    PopResult Pop(AudioPacket&, int64_t) override;
    void MarkEos() override;
};
```

- SPSC lock-free: one producer (encoder), one consumer (drain loop).
- Holds a fixed `slots[]` of `EncodedPacket`/`AudioPacket`; packets moved in/out.

## Consumer interface — `PacketConsumer`

Implemented by file sink and streamer. The drain loop (`PacketPump`) calls this.

```cpp
class PacketConsumer {
  public:
    virtual ~PacketConsumer() = default;
    virtual StatusCode Consume(EncodedPacket&& pkt) = 0;   // video
    virtual StatusCode Consume(AudioPacket&& pkt) = 0;     // audio
    virtual StatusCode Flush() { return StatusCode::kOk; }
    virtual StatusCode Finish() { return StatusCode::kOk; } // EOS / teardown
};
```

## Drain loop — `PacketPump`

Not an interface; a reusable helper that bridges source → consumer on the consumer
thread (see `output-queue.md` §3). Calls `Consume` for each popped packet; `Finish()` at
EOS; propagates `Consume` failures to logging/back-off.

## Concrete consumer: `FileSinkConsumer` (implement later)

Implements `PacketConsumer`. Contract obligations:
1. Preserve packet order; write `keyframe` packets (and SPS/PPS) at segment starts.
2. Raw bitstream: write Annex-B to `.h264`/`.aac`; **or** feed a muxer for
   `.mp4`/`.mkv` (muxing deferred — muxer is itself a `PacketConsumer`).
3. On `Finish()`: flush and close the file.
4. Must not block the pump indefinitely; file I/O errors return a `StatusCode`.

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
- encoder pushes via `OutputSink` and an independent `PacketPump`+`PacketConsumer`
  drains it with no packet loss (under `kBlock`) and correct order, AND
- swapping `FileSinkConsumer` for `StreamConsumer` requires no encoder change.
