# Tasks: Architecture & Engineering Design → Implementation (spec 002)

**Input**: Design documents from `specs/002-architecture-engineering-design/`
(`plan.md`, `spec.md`, `research.md`, `data-model.md`, `contracts/`) plus the
frozen architecture docs under `codec/doc/architecture/` and ADRs under
`codec/doc/adrs/`.

**Prerequisites**: spec `001-project-scaffold` (Bazel 6.5.0 workspace, FFmpeg
`rules_foreign_cc` static archive, platform `select()`, `make`-based validation,
module stubs under `codec/src/framework/`).

**Scope note**: spec 002 is a *design-only* spec; its contracts are the frozen
surface this task list implements. The Bazel workspace root is `codec/`; all
source paths below are relative to `codec/`. Module visibility follows
`codec/doc/architecture/module-dependencies.md` (`queue`/`consumer` depend only
on `core`; `consumer` also on `api`/`queue`; only `public` is `//visibility:public`).

**Tests**: Included where a contract defines an *Acceptance* section (ring buffer,
consumer, FFmpeg backend) — these are effectively required by the design's own
acceptance criteria, using the already-pinned googletest.

**Organization**: Tasks are grouped by capability/story so each can be built and
verified independently. Foundation first, then the encoder contract surface, then
the FFmpeg backend (MVP), then the output ring buffer + consumer (the transport
you asked for), then the public umbrella + example, then polish.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Capability cluster this task belongs to (US1..US5)
- Exact file paths included in every task

---

## Phase 1: Setup (Module scaffolding)

**Purpose**: Create the Bazel packages the design introduced but 001 did not stub
(`queue`, `consumer`), and confirm existing stubs expose the right visibility.

- [ ] T001 Create `queue` Bazel package: `codec/src/framework/queue/BUILD.bazel`
      with `package(default_visibility = ["//src/framework:__subpackages__", "//tests:__subpackages__"])`
      and a `cc_library(name = "queue", srcs = glob(["*.cc"]), hdrs = glob(["*.h"]))`.
- [ ] T002 Create `consumer` Bazel package: `codec/src/framework/consumer/BUILD.bazel`
      with the same visibility and a `cc_library(name = "consumer", ...)` that
      depends on `//src/framework:queue`, `//src/framework:api`, `//src/framework:core`.
- [ ] T003 [P] Verify `core`/`api`/`utils`/`backend/ffmpeg`/`backend/android`/`public`
      BUILD stubs already carry the module-dependencies visibility (no change if present);
      fix any that use a wider visibility than `//src/framework:__subpackages__`.

**Checkpoint**: `bazel build //src/framework/...` resolves (empty libs); `queue` and
`consumer` packages exist and are visible only inside the framework.

---

## Phase 2: Foundational (Core types, errors, logging)

**Purpose**: The types and cross-cutting utilities every other module depends on.
No story work starts until this is complete.

- [ ] T004 [P] Define `StatusCode` enum + `Result<T>` (expected<T>) in
      `codec/src/framework/core/status.h` and `codec/src/framework/core/result.h`
      per `error-handling.md` / `contracts/encoder-contract.md` (no exceptions cross
      the public API).
- [ ] T005 [P] Implement `LogSlot` abstract interface + process-wide no-op default in
      `codec/src/framework/core/log_slot.h` / `.cc` per `logging-slot.md` (default
      no-op; consumer plugs a concrete impl).
- [ ] T006 [P] Define media types in `core`: `VideoFrame`, `AudioFrame`,
      `EncodedPacket`, `AudioPacket`, `NativeBuffer` (pointer-object zero-copy
      convention) and shared enums (pixel fmt, codec, keyframe flag, `pts_us`) in
      `codec/src/framework/core/types.h` per `data-model.md` §3–§6.
- [ ] T007 [P] Add a `VIDEO_CODEC_API`-tagged umbrella of core public typedefs if
      needed by `public`; otherwise keep core internal (core has no public visibility).

**Checkpoint**: `core` compiles; `Result<T>` / `StatusCode` / `LogSlot` / media
types are usable from `api`, `queue`, `consumer`.

---

## Phase 3: User Story 1 — Encoder contract surface (Priority: P1)

**Goal**: Freeze the abstract `VideoEncoder` / `AudioEncoder` API, lifecycle state
machine, and factory/backend-selection exactly as in `contracts/encoder-contract.md`
and `contracts/public-api.md`.

**Independent Test**: A test that constructs an encoder via the factory with
`force_backend` and asserts the lifecycle rejects `Encode()` before `Init()` (returns
`StatusCode`, no crash).

### Implementation for User Story 1

- [ ] T008 [P] [US1] Declare `VideoEncoder` / `AudioEncoder` abstract interfaces
      (`Create`, `Init`, `Encode` CPU-frame + `NativeBuffer` overloads,
      `CreateInputSurface`, `Flush`, `Release`) in
      `codec/src/framework/api/video_encoder.h` / `audio_encoder.h` per
      `contracts/encoder-contract.md`.
- [ ] T009 [P] [US1] Declare `InputSurface` + `NativeBuffer` semantics in
      `codec/src/framework/api/input_surface.h` per `contracts/nativebuffer-contract.md`.
- [ ] T010 [US1] Implement lifecycle state machine `Created → Initialized → Encoding →
      Flushed → Released` returning errors on invalid transitions in
      `codec/src/framework/api/encoder_lifecycle.h` / `.cc` per `lifecycle-model.md`.
- [ ] T011 [US1] Implement factory + backend-selection (compile-time platform macro OR
      `force_backend`) in `codec/src/framework/api/encoder_factory.h` / `.cc` per
      `backend-selection.md` / ADR-002 (`select()`-per-platform backends).
- [ ] T012 [US1] Add lifecycle/selection unit tests in
      `codec/tests/api/encoder_lifecycle_test.cc` (invalid transition → error, not crash;
      `force_backend` overrides platform default).

**Checkpoint**: `api` builds; factory returns the platform-selected backend shim;
lifecycle enforces order. No real encoding yet (backends are stubbed).

---

## Phase 4: User Story 2 — FFmpeg backend (Priority: P1) 🎯 MVP

**Goal**: A real encoder end-to-end: feed CPU frames, get `Result<EncodedPacket>`
back (pull API) via `libavcodec`/`libx264`. This is the minimum that actually encodes.

**Independent Test**: Encode a synthetic 320×240 frame to H.264 and assert the output
is a valid Annex-B stream (reuse `ffmpeg_spike` pattern) using only the public API.

### Implementation for User Story 2

- [ ] T013 [P] [US2] Implement `FFmpegVideoEncoder` (libavcodec/libx264, `Init`/`Encode`/
      `Flush`/`Release`, SPS/PPS tagging, `keyframe` + `pts_us` on `EncodedPacket`) in
      `codec/src/framework/backend/ffmpeg/video_encoder.cc` / `.h` per
      `contracts/backend-contract.md`.
- [ ] T014 [P] [US2] Implement `FFmpegAudioEncoder` (AAC via `libavcodec`) in
      `codec/src/framework/backend/ffmpeg/audio_encoder.cc` / `.h`.
- [ ] T015 [US2] Register both behind the factory `select()` in
      `codec/src/framework/backend/ffmpeg/BUILD.bazel` (deps
      `//third_party/ffmpeg:ffmpeg_codec`; ADR-001 force-loaded archive).
- [ ] T016 [US2] Add integration test encoding a frame → valid H.264 in
      `codec/tests/backend/ffmpeg/encode_test.cc` (validates MVP: pull API returns
      encodable packets without the ring buffer).

**Checkpoint**: `bazel test //src/framework/backend/ffmpeg/...` passes — a valid
H.264 stream is produced through the public API. MVP achieved.

---

## Phase 5: User Story 3 — Output ring buffer (Priority: P2)

**Goal**: The encoder→consumer transport you designed: a bounded SPSC lock-free ring
buffer (`EncodedPacketQueue`) implementing `OutputSink` (producer) and
`EncodedPacketSource` (consumer), with configurable back-pressure (default `kBlock`).

**Independent Test**: Producer pushes N packets, consumer drains with `Pop(deadline)`
and receives all N in order with zero loss under `kBlock`; verify power-of-two
capacity + blocking back-pressure in unit tests.

### Implementation for User Story 3

- [ ] T017 [P] [US3] Declare `Backpressure` enum, `PopResult`, `OutputSink`,
      `EncodedPacketSource` interfaces in `codec/src/framework/queue/queue_iface.h`
      per `contracts/output-queue-contract.md`.
- [ ] T018 [US3] Implement `EncodedPacketQueue` (fixed power-of-two `slots[]`, atomic
      `head_`/`tail_`, move-in/move-out, `Submit` honoring `Backpressure`) in
      `codec/src/framework/queue/encoded_packet_queue.h` / `.cc`; constructor
      `EncodedPacketQueue(size_t capacity, Backpressure policy = Backpressure::kBlock)`
      per ADR-005 / `output-queue.md`.
- [ ] T019 [US3] Wire the encoder's optional `OutputSink` push mode: after a successful
      `Encode()`, forward `EncodedPacket&&` to a configured `OutputSink` (pull API still
      the default; push is opt-in) per `data-model.md` §8.
- [ ] T020 [US3] Add ring-buffer unit tests in `codec/tests/queue/encoded_packet_queue_test.cc`:
      SPSC correctness, power-of-two masking, `kBlock` blocks, `kDropOldest` overwrites,
      `kError` returns back-pressure code (contract *Acceptance*).

**Checkpoint**: Ring buffer is independently testable; encoder can push packets into it
with no loss under `kBlock`.

---

## Phase 6: User Story 4 — Consumer (PacketConsumer / PacketPump / FileSink) (Priority: P2)

**Goal**: The consumer side of the ring buffer: a `PacketPump` drain loop pops from
`EncodedPacketSource` and forwards to a `PacketConsumer`. Ship `FileSinkConsumer`
(write `.h264`/`.aac` or mux `.mp4`); define `StreamConsumer` contract (RTMP/SRT/WebRTC)
as deferred-but-designed.

**Independent Test**: End-to-end — encoder → `EncodedPacketQueue` → `PacketPump` →
`FileSinkConsumer` writes a valid `.h264` with correct order and no packet loss under
`kBlock`; swapping in a stub `StreamConsumer` requires no encoder change.

### Implementation for User Story 4

- [ ] T021 [P] [US4] Declare `PacketConsumer` interface (`Consume(EncodedPacket&&)`,
      `Consume(AudioPacket&&)`, `Flush`, `Finish`) and `PacketPump::Run` helper in
      `codec/src/framework/consumer/packet_consumer.h` / `packet_pump.h` per
      `contracts/output-queue-contract.md` + `output-queue.md` §3.
- [ ] T022 [US4] Implement `FileSinkConsumer` (Annex-B → `.h264` / `.aac`; preserve
      order + keyframe/SPS-PPS at segment start; flush/close on `Finish()`) in
      `codec/src/framework/consumer/file_sink_consumer.cc` / `.h`.
- [ ] T023 [US4] Add the end-to-end drain test in
      `codec/tests/consumer/file_sink_consumer_test.cc`: encoder → queue →
      `PacketPump` → `FileSinkConsumer` produces a valid file, in order, zero loss
      (contract *Acceptance*). Assert swapping `FileSinkConsumer` for a stub
      `StreamConsumer` needs no encoder change.
- [ ] T024 [US4] Add `StreamConsumer` abstract contract note/header (RTMP/SRT/WebRTC
      deferred per `project_bootstrap.md`) in `codec/src/framework/consumer/stream_consumer.h`
      documenting framing / connection lifecycle / pacing / back-pressure obligations
      from `output-queue.md` §4.2 (no network client yet).

**Checkpoint**: The full transport you designed works end-to-end to a file; the
encoder is transport-agnostic (file ↔ stream is a one-line `PacketConsumer` swap).

---

## Phase 7: User Story 5 — Public umbrella, example, verification (Priority: P3)

**Goal**: Expose the library through `public`, ship a runnable encode-to-file example,
and wire the `make`-based validation + CI matrix.

**Independent Test**: `make verify` (or the CI job) builds + runs the example that
encodes a clip to `.h264` via `queue` + `FileSinkConsumer` and asserts the file is valid.

### Implementation for User Story 5

- [ ] T025 [P] [US5] Finalize public umbrella header + `VIDEO_CODEC_API` export in
      `codec/src/framework/public/include/video_codec/video_codec.h` per
      `contracts/public-api.md`; keep `public` as the only `//visibility:public` module.
- [ ] T026 [US5] Add example `codec/src/examples/encode_to_file.cc` that wires
      `FFmpegVideoEncoder` → `EncodedPacketQueue` → `PacketPump` → `FileSinkConsumer`.
- [ ] T027 [US5] Register the example + a smoke target in `codec/src/examples/BUILD.bazel`
      and add it to `codec/mk/` + `scripts/verify/` categorized validation (per spec 001
      mechanism).
- [ ] T028 [US5] Confirm CI matrix (`codec/.github/workflows/ci.yml`, `android.yml`)
      builds + tests on macOS ARM64 / Linux x86_64 / Android cross-build; add the
      example/smoke job if missing.

**Checkpoint**: A consumer can `encode_to_file` through the public API and the ring
buffer; CI is green on the target matrix.

---

## Phase N: Polish & Cross-Cutting Concerns

**Purpose**: Consistency audit (the kind we just did for back-pressure), docs, release.

- [ ] T029 [P] Run a cross-doc consistency audit: back-pressure default (`kBlock`) agrees
      across `research.md` R9, `data-model.md`, `output-queue.md`, ADR-005,
      `output-queue-contract.md`; module graph in `plan.md` matches
      `module-dependencies.md` (incl. `consumer` node).
- [ ] T030 [P] Update `CHANGELOG.md` "Unreleased" with the implemented modules and the
      ring-buffer + consumer transport.
- [ ] T031 Run `codec/doc/quickstart.md` validation end-to-end and fix any drift between
      docs and the implemented BUILD/test layout.
- [ ] T032 Optional: author `StreamConsumer` network clients (RTMP/SRT/WebRTC) as a
      follow-on spec once `FileSinkConsumer` + `PacketPump` are proven.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No deps — start immediately.
- **Foundational (Phase 2)**: Depends on Setup — BLOCKS all stories.
- **US1 (Phase 3)**: Depends on Foundational.
- **US2 (Phase 4, MVP)**: Depends on Foundational + US1 (factory returns the backend).
- **US3 (Phase 5)**: Depends on Foundational (core types) — independent of US1/US2.
- **US4 (Phase 6)**: Depends on US3 (queue) + US1 (`PacketConsumer` consumes
  `EncodedPacket` from `api`/`core`); can start once US3 lands.
- **US5 (Phase 7)**: Depends on US2 + US4 (needs a real backend + consumer to wire the
  example).
- **Polish (Phase N)**: Depends on all desired stories.

### User Story Dependencies

- **US1 (P1)**: After Foundational — no dependency on other stories.
- **US2 (P1, MVP)**: After Foundational + US1 — may integrate with US1 only.
- **US3 (P2)**: After Foundational — independent of US1/US2; pairs with US4.
- **US4 (P2)**: After US3 + US1.
- **US5 (P3)**: After US2 + US4.

### Within Each User Story

- Interfaces/contracts before implementation.
- Core types (Phase 2) before any story.
- Implementation before its test/acceptance task.
- Story complete before the next priority.

### Parallel Opportunities

- All Setup tasks marked [P] (T001/T002/T003) can run in parallel.
- All Foundational tasks marked [P] (T004–T007) can run in parallel within Phase 2.
- US3 (ring buffer) and US1 (encoder surface) can be developed in parallel after
  Foundational — they share only `core` types.
- Within a story, interface declaration [P] tasks precede the implementation task.

---

## Parallel Example: Phase 5 (Output ring buffer) + Phase 3 (Encoder surface)

```bash
# After Foundational (Phase 2) completes:
# US3 — ring buffer (independent of encoder surface):
Task: "Declare Backpressure/PopResult/OutputSink/EncodedPacketSource in queue/queue_iface.h"
Task: "Implement EncodedPacketQueue SPSC in queue/encoded_packet_queue.{h,cc}"
# US1 — encoder surface (independent of ring buffer):
Task: "Declare VideoEncoder/AudioEncoder abstract in api/video_encoder.h"
Task: "Implement lifecycle state machine in api/encoder_lifecycle.{h,cc}"
```

---

## Implementation Strategy

### MVP First (US2 only — real encoding)

1. Complete Phase 1 (Setup) + Phase 2 (Foundational).
2. Complete Phase 3 (US1 encoder surface) + Phase 4 (US2 FFmpeg backend).
3. **STOP and VALIDATE**: `encode_test.cc` produces a valid H.264 stream via the pull API.
4. Demo: encode to in-memory packets works.

### Incremental Delivery (add the transport you designed)

1. Setup + Foundational → foundation ready.
2. Add US1 + US2 → MVP (encode to packets). **Demo.**
3. Add US3 (ring buffer) → encoder can push into a bounded SPSC queue. **Demo.**
4. Add US4 (consumer) → encoder → queue → `FileSinkConsumer` writes a valid file,
   transport-agnostic. **Demo** (this is the architecture from your request).
5. Add US5 (public + example + CI) → `encode_to_file` example + green CI matrix.
6. Polish (Phase N) → consistency audit, CHANGELOG, quickstart validation.

### Parallel Team Strategy

- Developer A: Phase 2 Foundational, then US1 (encoder surface).
- Developer B (after Foundational): US3 (ring buffer) in parallel with A's US1.
- Developer C (after US3 + US1): US4 (consumer), then US5 (public/example).
- US2 (FFmpeg backend) is the MVP critical path — staff early.

---

## Notes

- [P] tasks = different files, no dependencies.
- [Story] label maps a task to its capability cluster for traceability.
- Each story is independently completable and testable.
- Commit after each task or logical group (spec-kit `after_tasks` hook can auto-commit).
- Stop at any checkpoint to validate the story independently.
- The ring buffer + consumer (US3/US4) is the transport you explicitly asked for
  ("编码输出到环形队列,消费者从环形队列取数据的架构"); US1/US2 are the prerequisites
  that make it runnable.
