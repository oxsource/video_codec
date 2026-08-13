# Data Model: Encoder-to-Queue Push Wiring (spec 004)

**Branch**: `004-encoder-queue-wiring` | **Date**: 2026-08-12
**Feature**: [spec.md](spec.md) | **Research**: [research.md](research.md)

No new persistent entities. This feature adds one **attachment relationship** (encoder → sink)
and a **mode flag** (push vs pull) to the existing encoder entities.

## 1. Entities

### Encoder (VideoEncoder / AudioEncoder, abstract)
- Gains an **optional output-sink attachment** (`OutputSink*`), set via
  `SetOutputSink(OutputSink*)`.
- Gains an implicit **mode**: pull (no sink) — default, unchanged; push (sink attached).
- Owns the pointer non-owningly; must clear it on `Release()`.

### OutputSink (existing, `queue/queue_iface.h`)
- Producer endpoint of the queue. `Submit(Packet&&)`, `Submit(Packet&&)Submit(Packet&&)`,
  `Flush()`.
- Applies the queue's back-pressure policy; returns `StatusCode`.

### PacketQueue (existing)
- Implements `OutputSink` + `PacketSource`.
- `MarkEos()` is called by the **caller** (not the encoder) once all producers are done.

### Packet (existing)
- Payload handed from encoder to sink in order. In push mode the packet is **moved** into
  the sink; the encoder's returned packet is empty (moved-from).

## 2. Relationship

```text
Encoder --SetOutputSink--> OutputSink (queue producer end)
   Encode() produces Packet
      push: Submit(Packet&&)  -> queue (single destination)
      pull: return Packet     -> caller (default, unchanged)
   Flush(): drain final packet -> sink, then sink->Flush()
Caller (owns queue): after all encoders done -> queue.MarkEos()
```

## 3. Validation rules (from spec FR / research)

- `SetOutputSink(nullptr)` clears the attachment (back to pull-only).
- `SetOutputSink(non-null)` on a backend that does not support push returns
  `kUnsupportedOperation` and leaves the encoder in pull mode.
- Push mode active ⇒ each produced packet goes to exactly ONE destination (the sink);
  `Encode()` returns `kOk` with an empty packet.
- Sink `Submit` errors (e.g., `kBackendUnavailable` under `kError` policy) propagate to the
  caller as the `Encode()` result.
- Lifecycle transitions are unchanged (encode before init still `kNotInitialized`).
- `Release()` detaches the sink and nulls the stored pointer.

## 4. State transitions

- Encoder lifecycle (Created → Initialized → Encoding → Flushed → Released) is unchanged.
- Mode is fixed at attachment time and does not transition during encoding.
