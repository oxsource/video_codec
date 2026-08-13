# Push-Mode Contract: Encoder → OutputSink (spec 004)

**Branch**: `004-encoder-queue-wiring` | **Date**: 2026-08-12
**Feature**: [spec.md](../spec.md) | **Data model**: [data-model.md](../data-model.md) | **Research**: [research.md](../research.md)

Defines the encoder-side push wiring added by this feature. It does not change the frozen
`OutputSink`/`PacketSource` contracts in `contracts/output-queue-contract.md`
(spec 002) or the encoder lifecycle contract.

## 1. Attachment contract

```cpp
// On both VideoEncoder and AudioEncoder (api headers; OutputSink is fwd-declared):
//   Attach a sink to enable push mode. Pass nullptr to detach (back to pull-only).
//   Backends without push support return Status::kUnsupportedOperation and
//   stay in pull mode.
virtual Status SetOutputSink(OutputSink* sink);
```

- Default implementation: `return Status::kUnsupportedOperation;`
- Non-owning: the caller keeps the sink alive for the encoder's lifetime; `Release()`
  clears the pointer.

## 2. Mode semantics

| Mode | Trigger | `Encode()`/`Flush()` result |
|------|---------|------------------------------|
| Pull (default) | no sink attached | full `Packet` returned (unchanged) |
| Push | non-null sink attached | packet **moved into the sink**; returns `kOk` with an empty (moved-from) packet |

- A produced packet reaches **exactly one destination** (FR-006). No duplication.
- Sink `Submit` errors propagate as the `Encode()`/`Flush()` result.

## 3. Flush & end-of-stream

- On `Flush()` in push mode: the encoder drains any final packet to the sink, then calls
  `sink->Flush()`.
- **End-of-stream is caller-owned**: after all encoders are flushed/done, the caller calls
  `PacketSource::MarkEos()` on the queue so the consumer terminates with `kEos`
  (multi-producer safety — see research R4). This refines spec FR-005 (encoder flushes the
  sink; caller marks EOS) and satisfies its acceptance outcome.

## 4. Back-pressure

- The sink (queue) applies its configured policy (`kBlock` default, `kDropOldest`, `kError`).
- The encoder makes no independent pacing decisions; it honors the `Status` returned by
  `Submit`. Under `kBlock` a full queue naturally blocks the producer.

## 5. Audio

- `AudioEncoder` supports the same attachment and pushes audio `Packet`s via
  `OutputSink::Submit(Packet&&)`.

## 6. Acceptance

- **A1**: A real encoder with a sink attached delivers all N produced packets to the queue in
  order with zero loss (under `kBlock`), verified end-to-end.
- **A2**: With no sink attached, the existing pull behavior passes the existing encoder tests
  unchanged.
- **A3**: `Flush()` pushes the final packet and calls `sink->Flush()`; after caller
  `MarkEos()`, the consumer observes `kEos` exactly once.
- **A4**: `SetOutputSink` on a push-incapable backend returns `kUnsupportedOperation` and
  leaves the encoder in pull mode.
- **A5**: Audio push delivers audio packets (`PacketType::kAudio`) to the sink with the
  same guarantees.
