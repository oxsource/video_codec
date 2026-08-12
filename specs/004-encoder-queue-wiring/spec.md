# Feature Specification: Encoder-to-Queue Push Wiring

**Feature Branch**: `004-encoder-queue-wiring`

**Created**: 2026-08-12

**Status**: Draft

**Input**: User description: "编码器→队列接线" (wire the encoder to the output queue).

## User Scenarios & Testing *(mandatory)*

The library already has: a working encoder (pull API — the caller calls `Encode()` and
receives `EncodedPacket`s back), and a bounded SPSC ring buffer (`EncodedPacketQueue`)
with a producer endpoint (`OutputSink`) and a consumer endpoint (`EncodedPacketSource`).
What is missing is the **link between them**: a real encoder currently does not feed the
ring buffer. This feature wires the encoder so that, when a caller opts in, every packet
produced by a successful encode is **automatically pushed** into the output queue — while
keeping the existing pull API as the default. This is the missing piece that makes the
designed "encoder → queue → consumer → file" transport work end-to-end with a real encoder
instead of synthetic test packets.

### User Story 1 - Encoder pushes encoded output to a queue (Priority: P1)

A consumer who wants streaming/file transport wires an encoder to an output queue once; from
then on, each frame they feed in results in its encoded packet(s) appearing in the queue
automatically, in order, with no packet loss under the default blocking policy. The caller
does not need to collect packets manually from the pull API.

**Why this priority**: Without this link, the encoder and the queue/consumer transport are
disconnected — the end-to-end "encode to file" flow cannot run with a real encoder. This is
the critical missing integration.

**Independent Test**: An automated test wires a real encoder to an in-memory output queue,
feeds N frames, drains the queue, and asserts all N encoded packets arrive in order with
zero loss. The pull API must still work when push is not configured.

**Acceptance Scenarios**:

1. **Given** an encoder configured with push mode enabled, **When** N frames are encoded,
   **Then** exactly N packets arrive in the queue in encode order with zero loss (under the
   default blocking back-pressure).
2. **Given** an encoder NOT configured for push, **When** frames are encoded, **Then** the
   existing pull API returns packets exactly as before and nothing is pushed.
3. **Given** a push-mode encoder, **When** it is flushed, **Then** the final flushed packet
   (if any) is pushed and the queue is marked end-of-stream so the consumer drains cleanly.

---

### User Story 2 - Queue back-pressure governs encode pacing (Priority: P2)

When the consumer drains slower than the encoder produces, a full queue must slow the
encoder (blocking back-pressure) instead of dropping or corrupting packets, giving natural
end-to-end flow control.

**Why this priority**: Correct back-pressure is what makes the push path safe for
real-time/streaming use; without it, a slow consumer would lose data.

**Independent Test**: An automated test with a deliberately slow consumer verifies that the
producer blocks on a full queue and that after the consumer catches up, all packets are
delivered in order with zero loss.

**Acceptance Scenarios**:

1. **Given** a slow consumer and a bounded queue, **When** the producer fills the queue,
   **Then** the producer blocks rather than overwriting packets, and no packet is lost.
2. **Given** the configured back-pressure policy, **When** the queue is full, **Then** the
   policy (`block` / `drop-oldest` / `error`) is honored exactly as documented.

---

### Edge Cases

- What happens when push mode is enabled but no queue is attached? → Either an explicit
  configuration error or a clear no-op (documented default: treating it as pull-only and
  reporting a configuration error on `Init`).
- What happens if a single `Encode()` call produces zero packets (e.g., frame buffering)? →
  Nothing is pushed; the queue is unaffected; the caller is not misled.
- How are flush-produced final packets handled? → They are pushed before the queue is marked
  end-of-stream, so the consumer receives them before `kEos`.
- What about audio packets? → The same push wiring applies to the audio encoder and its
  `AudioPacket` output.
- If push and pull are both active, do packets get duplicated? → No: push mode replaces the
  caller's manual collection; a single packet goes to exactly one destination.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: An encoder MUST be configurable to deliver each encoded output packet to a
  caller-provided output sink immediately after it is produced (push mode), in addition to
  or instead of returning it via the pull API.
- **FR-002**: When push mode is NOT configured, the existing pull behavior MUST be unchanged
  and MUST remain the default.
- **FR-003**: In push mode, packets MUST be delivered in encode order with zero loss under
  the default blocking back-pressure policy.
- **FR-004**: The encoder MUST honor the configured back-pressure policy when the sink is
  full (block, drop-oldest, or error), without corrupting the encoder's internal state.
- **FR-005**: On flush, the encoder MUST push any final drained packet to the sink and then
  mark the end-of-stream so the consumer can terminate cleanly.
- **FR-006**: A single encoded packet MUST be delivered to exactly one destination (push
  sink OR pull return), never both, when both modes are conceptually active.
- **FR-007**: The audio encoder MUST support the same push wiring for `AudioPacket` output.
- **FR-008**: Enabling push mode with no sink attached MUST be reported as a configuration
  error rather than silently discarding output.
- **FR-009**: The wiring MUST NOT change the encoder lifecycle contract — invalid-state
  transitions (e.g., encode before init) still return an error status.
- **FR-010**: The wiring MUST be independently testable without a real consumer (in-memory
  sink), and the existing pull-mode tests MUST continue to pass.

### Key Entities

- **Encoder (video/audio)**: produces encoded output; gains an optional output-sink
  attachment and a push mode.
- **OutputSink**: the producer endpoint of the queue (or any sink); receives submitted
  encoded/audio packets.
- **EncodedPacketQueue**: the bounded ring buffer the sink fronts; applies the configured
  back-pressure policy.
- **EncodedPacket / AudioPacket**: the payloads handed from encoder to sink in order.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With push mode on, 100% of encoded packets from a multi-frame encode reach the
  sink in order, with zero loss, verified by an automated end-to-end test.
- **SC-002**: With push mode off, the existing pull-API behavior passes 100% of the existing
  encoder tests unchanged.
- **SC-003**: Under a deliberately slow consumer, the producer blocks on a full queue and
  zero packets are lost, verified by an automated back-pressure test.
- **SC-004**: A flush drains the final packet(s) to the sink and the consumer observes a
  clean end-of-stream exactly once, verified by an automated test.
- **SC-005**: Enabling push without a sink produces a clear configuration error (and no
  silent packet loss), verified by an automated test.

## Assumptions

- The push mode is **opt-in**: default encoder behavior remains the pull API (consistent
  with the architecture's "pull API still the default; push is opt-in" decision in
  `data-model.md` §8 of spec `002-architecture-engineering-design`).
- The sink is attached at encoder configuration/initialization time and is owned by the
  caller; the encoder holds a non-owning reference and must not use it after release.
- Back-pressure default is blocking (`kBlock`), per the frozen contract
  (`contracts/output-queue-contract.md`).
- The consumer drain loop (`PacketPump`) and `FileSinkConsumer` already exist and are
  unchanged by this feature; this feature only wires the encoder side.
- Encoders are not internally thread-safe (one encoder per thread, per the threading model);
  push wiring preserves this contract — no new internal threads are introduced.
- This implements the still-open task T020 in the spec-002 task list
  (`specs/002-architecture-engineering-design/tasks.md`).
