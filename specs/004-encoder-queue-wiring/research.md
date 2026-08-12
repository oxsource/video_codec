# Research: Encoder-to-Queue Push Wiring (spec 004)

**Branch**: `004-encoder-queue-wiring` | **Date**: 2026-08-12
**Feature**: [spec.md](spec.md)

Resolves the open technical questions for wiring the encoder to the output queue. The queue
(`EncodedPacketQueue`, producer `OutputSink` / consumer `EncodedPacketSource`) and the
consumer transport already exist; this feature only adds the encoder-side push path.

## R1 — Where `OutputSink` is referenced vs. module dependencies

- **Decision**: Add `virtual StatusCode SetOutputSink(OutputSink* sink)` to the abstract
  `VideoEncoder` and `AudioEncoder` in `api`, **forward-declaring** `OutputSink` in the api
  headers (no `#include`). The default implementation returns
  `StatusCode::kUnsupportedOperation`. Only the FFmpeg backends include
  `queue/queue_iface.h` and override the hook; the `backend/ffmpeg` BUILD gains a
  dependency on `//src/framework/queue`.
- **Rationale**: The module-dependency rule (`module-dependencies.md` in spec 002) keeps
  `api` dependent only on `core`; `queue` is a sibling. A forward-declared pointer parameter
  lets the contract surface live on the public API without a compile-time dependency.
  `backend/ffmpeg → queue` is acyclic (queue depends only on core) and matches the design's
  hand-off role (`plan.md` mermaid: backend → `OutputSink/EncodedPacket` → queue).
- **Alternatives considered**:
  - `api` depends on `queue`: simplest compile-wise, but broadens `api`'s dependency surface
    against the frozen module graph — rejected.
  - Move `OutputSink` into `core`: would relocate a frozen contract — rejected.
  - A separate push adapter wrapping the encoder (composition): keeps the encoder untouched,
    but task T020 explicitly calls for the sink on the encoder base, and the adapter would
    duplicate lifecycle handling — rejected.

## R2 — Push + pull semantics (single destination)

- **Decision**: When a sink is attached, the produced packet is **moved into the sink**; the
  packet is the sink's to hold. `Encode()`/`Flush()` still return `Result<...>` with `kOk`
  but carry an **empty** (moved-from) packet. When no sink is attached, pull returns the
  packet exactly as today.
- **Rationale**: FR-006 requires one destination. Handing ownership to the sink and returning
  an empty packet is the least surprising way to keep the call contract intact (status +
  lifecycle still meaningful) while guaranteeing no duplication.
- **Alternatives considered**: Returning the packet AND submitting it (both destinations) —
  violates FR-006 (duplication risk) — rejected. Returning the packet and submitting a copy
  — adds a copy, violates the "no surprise copies on hot path" goal — rejected.

## R3 — Back-pressure ownership

- **Decision**: The sink (`OutputSink::Submit`) applies the queue's configured back-pressure
  policy; the encoder simply calls `Submit(EncodedPacket&&)` and propagates its `StatusCode`.
  No new pacing logic in the encoder; blocking (`kBlock`) naturally paces the producer.
- **Rationale**: Back-pressure already lives in the queue (frozen contract,
  `output-queue-contract.md`). The encoder must honor whatever the sink returns (including
  `kBackendUnavailable` under `kError`).
- **Alternatives considered**: Encoder-side buffering to absorb pressure — adds unbounded
  memory and violates the bounded-ring design — rejected.

## R4 — End-of-stream ownership

- **Decision**: On `Flush()` in push mode the encoder drains the final packet to the sink and
  calls `sink->Flush()`. Marking end-of-stream (`EncodedPacketSource::MarkEos()`) is the
  **caller's** job after all producers (e.g., video + audio) are done.
- **Rationale**: Multiple encoders may share one queue; only the caller knows when the whole
  stream is complete. If an encoder auto-marked EOS on its own flush, a video encoder would
  EOS a queue still expecting audio. This refines FR-005 (encoder flushes the sink; caller
  marks EOS) while preserving the spec's acceptance outcome ("consumer drains cleanly").
- **Alternatives considered**: Encoder calls `MarkEos()` itself after flush (requires the
  backend to `dynamic_cast` the sink to `EncodedPacketSource*`) — breaks the multi-producer
  case — rejected.

## R5 — Testing approach

- **Decision**: googletest in a new `codec/tests/backend/ffmpeg/` package:
  (a) real FFmpeg video encoder → `EncodedPacketQueue(kBlock)` → drain N frames: order +
  zero loss; (b) no sink attached: `Encode()` still returns full packets (pull unchanged);
  (c) flush pushes the final packet then `sink->Flush()`; caller `MarkEos()` → consumer sees
  `kEos`; (d) `SetOutputSink` returns `kUnsupportedOperation` on a stub/no-push encoder
  (api-level). Audio push covered analogously.
- **Rationale**: The independent-test criteria in the spec require a real encoder; FFmpeg is
  already built/cached in this workspace, so the integration test links without new external
  work. The api-level negative test proves the contract default.
- **Alternatives considered**: Testing only against a fake encoder — would not prove the real
  push path — rejected as insufficient for the end-to-end acceptance.

## Open items resolved

All NEEDS CLARIFICATION resolved. The four design decisions above (fwd-declared sink in api,
single-destination push, queue-owned back-pressure, caller-owned EOS) are recorded in the
push-mode contract and will drive the task list.
