# ADR-005: Encoded-output transport via a bounded SPSC ring buffer

- **Status**: Accepted
- **Date**: 2026-08-12

## Context

Encoded output (video `EncodedPacket`, audio `AudioPacket`) is produced by an encoder on
one thread and must be consumed elsewhere — a muxer, a network sender, or a file writer.
Two naive shapes do not fit this framework:

- **Return-to-caller (`Encode()` returns `Result<EncodedPacket>`)** is correct and stays
  as the default pull API, but it forces the producer and consumer onto the *same* thread
  and couples encode rate to consume rate. As soon as the consumer can block (network
  stall, disk flush) the encoder stalls with it.
- The framework's threading model is **synchronous / caller-owns-thread**
  (see `threading.md`): an encoder instance is *not* internally thread-safe. So any
  cross-thread hand-off must carry its own synchronization, independent of the encoder.

We need a transport that (a) decouples encode rate from consume rate, (b) carries its own
thread-safety so the encoder and consumer can live on different threads, (c) avoids
allocations on the hot `Encode()`-to-output path (a performance goal from `spec.md`), and
(d) does not pull a heavyweight dependency (boost.lockfree, folly) into a static library.

## Decision

Encoded output is handed off through a **bounded single-producer / single-consumer (SPSC)
lock-free ring buffer**, `EncodedPacketQueue`, living in a new `queue` module that depends
only on `core`.

- The same queue object exposes **two** interfaces:
  - `OutputSink` (producer endpoint) — `Submit(EncodedPacket&&)` / `Submit(AudioPacket&&)`
    / `Flush()`.
  - `EncodedPacketSource` (consumer endpoint) — `TryPop(EncodedPacket&)` / `TryPop(AudioPacket&)`
    plus a blocking `Pop(deadline)` variant.
- The encoder, after a successful `Encode()`, *optionally* forwards the packet to a
  configured `OutputSink`. The existing pull API (`Encode()` returning
  `Result<EncodedPacket>`) is unchanged; pushing is an opt-in sink mode.
- The ring uses fixed power-of-two capacity, pre-allocated `slots[]`, and
  `std::atomic<size_t>` `head_`/`tail_` indices masked by `(capacity − 1)`. Packets are
  **moved** in and out (no copy on the hot path).
- Full-vs-not behavior is governed by a `Backpressure` policy:
  `kBlock` (producer waits), `kDropOldest` (overwrites the oldest unconsumed slot — for
  live/real-time pipelines where staleness is worse than loss), `kError` (returns a
  back-pressure status code). Default `kError` to fail loud.

## Consequences

- Encoder and consumer run on independent threads; the encoder never blocks on consumer
  work beyond the chosen `Backpressure` policy.
- No allocations on the produce/consume hot path (slots are pre-allocated; packets move).
- `queue` is a leaf-ish module depending only on `core`, so it is trivially testable and
  reusable by any backend without pulling in `api`/`utils`/external encoder deps.
- The pull API remains the default; the ring is additive, not a breaking change.
- SPSC (not MPMC) is deliberate: the framework has exactly one encoder instance producing
  and (typically) one consumer draining. MPMC would add CAS contention and complexity we
  do not need.
- `kDropOldest` can silently discard packets; consumers needing lossless delivery must
  choose `kBlock` or `kError` and size capacity for their burst.

## Alternatives rejected

- **Return-to-caller only**: couples producer/consumer threads; blocks the encoder on
  consumer stalls. Kept as the default API but insufficient alone.
- **Unbounded queue**: unbounded memory growth under a slow consumer; unacceptable for a
  media pipeline that may run for hours.
- **`std::mutex` + `std::queue`**: correct and simple, but the lock is on the hot path and
  the framework's performance goal calls out avoiding surprise allocations/locks there;
  SPSC atomics are cheaper and lock-free for the common case.
- **MPMC lock-free (boost.lockfree / folly)**, external dep: we have one producer and one
  consumer, so the extra generality is wasted complexity and a heavy dependency in a static
  library. Revisit only if a multi-consumer fan-out requirement appears.
