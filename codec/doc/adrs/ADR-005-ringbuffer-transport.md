# ADR-005: Ring-buffer transport with pluggable consumer (file / stream)

- **Status**: Accepted
- **Date**: 2026-08-12

## Context

Encoded packets must reach a consumer that could be a **file writer** (save `.h264` /
mux to `.mp4`) or a **network streamer** (RTMP / SRT / WebRTC). These consumers are
implemented later, but the architecture must anticipate both now so the encoder is not
coupled to either. The encoder runs synchronously on its own thread; the consumer runs
on another. We need a decoupled, rate-decoupled hand-off with configurable back-pressure.

## Decision

Use a **bounded SPSC ring buffer** (`PacketQueue`) between encoder and consumer:
- Producer endpoint: `OutputSink::Submit(Packet&&)` — the encoder pushes.
- Consumer endpoint: `PacketSource::Pop(...)` — a `PacketPump` drain loop pops.
- Packets moved (no per-packet allocation); atomic `head_`/`tail_` over a power-of-two
  slot array; lock-free SPSC.
- A `PacketConsumer` interface (`Consume` / `Flush` / `Finish`) is implemented by both
  `FileSinkConsumer` and `StreamConsumer`; the pump forwards popped packets to it.
- Back-pressure when full is configurable: `kBlock` (default) / `kDropOldest` / `kError`.

## Consequences

- The encoder is **transport-agnostic**: swapping file output for streaming is a
  one-line `PacketConsumer` change; no encoder edits.
- Natural end-to-end back-pressure: a slow stream socket → slow `Consume` → filled ring →
  encoder slows (under `kBlock`). No unbounded memory, no silent frame loss.
- SPSC lock-free avoids mutex contention on the encode hot path.
- Multiple simultaneous consumers (record + stream) use two queues or a tee — the SPSC
  contract stays clean (one reader per queue).

## Alternatives rejected

- *Encoder returns packet, app manages its own queue*: pushes a non-trivial concurrency
  decision onto every consumer; no shared, tested component.
- *Unbounded queue*: unbounded memory under a slow consumer.
- *Mutex + `std::queue`*: correct but lock contention per encode; SPSC lock-free is
  strictly better for one-writer/one-reader.
- *MPMC queue*: more general than needed; harder to make wait-free for the encode→mux
  path.
- *Encoder writes directly to file/socket*: couples the encoder to each sink; cannot
  later add streaming without editing the encoder.
