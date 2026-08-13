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

**Status legend**: `[x]` = already implemented (commit `628e5be`); `[ ]` = pending
proposal. The `utils` module (T008) was a gap in the original breakdown and is
added here — it is foundational and must land in Phase 2, before US2/US5 consume it.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Capability cluster this task belongs to (US1..US5)
- Exact file paths included in every task

---

## Phase 1: Setup (Module scaffolding)

**Purpose**: Create the Bazel packages the design introduced but 001 did not stub
(`queue`, `consumer`), and confirm existing stubs expose the right visibility.

- [x] T001 Create `queue` Bazel package: `codec/src/framework/queue/BUILD.bazel`
      with `package(default_visibility = ["//src/framework:__subpackages__", "//tests:__subpackages__"])`
      and a `cc_library(name = "queue", srcs = glob(["*.cc"]), hdrs = glob(["*.h"]))`.
- [x] T002 Create `consumer` Bazel package: `codec/src/framework/consumer/BUILD.bazel`
      with the same visibility and a `cc_library(name = "consumer", ...)` that
      depends on `//src/framework:queue`, `//src/framework:api`, `//src/framework:core`.
- [x] T003 [P] Verify `core`/`api`/`utils`/`backend/ffmpeg`/`backend/android`/`public`
      BUILD stubs already carry the module-dependencies visibility (no change if present);
      fix any that use a wider visibility than `//src/framework:__subpackages__`.

**Checkpoint**: `bazel build //src/framework/...` resolves (empty libs); `queue` and
`consumer` packages exist and are visible only inside the framework. ✅ done.

---

## Phase 2: Foundational (Core types, errors, logging, utils)

**Purpose**: The types and cross-cutting utilities every other module depends on.
No story work starts until this is complete.

- [x] T004 [P] Define `Status` enum + `Result<T>` (expected<T>) in
      `codec/src/framework/core/status.h` and `codec/src/framework/core/result.h`
      per `error-handling.md` / `contracts/encoder-contract.md` (no exceptions cross
      the public API).
- [x] T005 [P] Implement `LogSlot` abstract interface + process-wide no-op default in
      `codec/src/framework/core/log_slot.h` / `.cc` per `logging-slot.md` (default
      no-op; consumer plugs a concrete impl).
- [x] T006 [P] Define media types in `core`: `VideoFrame`, `AudioFrame`,
      `Packet`, `NativeBuffer` (pointer-object zero-copy
      convention) and shared enums (pixel fmt, codec, keyframe flag, `pts_us`) in
      `codec/src/framework/core/types.h` per `data-model.md` §3–§6.
- [x] T007 [P] Add a `VIDEO_CODEC_API`-tagged umbrella of core public typedefs if
      needed by `public`; otherwise keep core internal (core has no public visibility).
      Resolved: core stays internal — no public typedef surface needed.
- [ ] T008 [P] Implement the `utils` module per `plan.md` module graph
      (`YUV420P↔NV12`, stride helpers, PCM sample-format conversion) in
      `codec/src/framework/utils/` (BUILD stub already exists with
      `deps = ["//src/framework/core"]`). **Foundational gap**: the original
      breakdown never scheduled this module even though `module-dependencies.md` and
      the architecture graph depend on it. Land before US2/US5 consume it.

**Checkpoint**: `core` + `utils` compile; `Result<T>` / `Status` / `LogSlot` /
media types / pixel-format + PCM helpers are usable from `api`, `queue`, `consumer`.

---

## Phase 3: User Story 1 — Encoder contract surface (Priority: P1)

**Goal**: Freeze the abstract `VideoEncoder` / `AudioEncoder` API, lifecycle state
machine, and factory/backend-selection exactly as in `contracts/encoder-contract.md`
and `contracts/public-api.md`.

**Independent Test**: A test that constructs an encoder via the factory with
`backend` and asserts the lifecycle rejects `Encode()` before `Init()` (returns
`Status`, no crash).

### Implementation for User Story 1

- [x] T009 [P] [US1] Declare `VideoEncoder` / `AudioEncoder` abstract interfaces
      (`Create`, `Init`, `Encode` CPU-frame + `NativeBuffer` overloads,
      `CreateInputSurface`, `Flush`, `Release`) in
      `codec/src/framework/api/video_encoder.h` / `audio_encoder.h` per
      `contracts/encoder-contract.md`.
- [x] T010 [P] [US1] Declare `InputSurface` + `NativeBuffer` semantics in
      `codec/src/framework/api/input_surface.h` per `contracts/nativebuffer-contract.md`.
- [x] T011 [US1] Implement lifecycle state machine `Created → Initialized → Encoding →
      Flushed → Released` returning errors on invalid transitions in
      `codec/src/framework/api/encoder_lifecycle.h` / `.cc` per `lifecycle-model.md`.
- [x] T012 [US1] Implement factory + backend-selection (compile-time platform macro OR
      `backend`) in `codec/src/framework/api/encoder_factory.h` / `.cc` per
      `backend-selection.md` / ADR-002 (`select()`-per-platform backends).
- [x] T013 [US1] Add lifecycle/selection unit tests in
      `codec/tests/api/encoder_lifecycle_test.cc` (invalid transition → error, not crash;
      `backend` overrides platform default).

**Checkpoint**: `api` builds; factory returns the platform-selected backend shim;
lifecycle enforces order. No real encoding yet (backends are stubbed). ✅ done.

---

## Phase 4: User Story 2 — FFmpeg backend (Priority: P1) 🎯 MVP

**Goal**: A real encoder end-to-end: feed CPU frames, get `Result<Packet>`
back (pull API) via `libavcodec`/`libx264`. This is the minimum that actually encodes.

**Independent Test**: Encode a synthetic 320×240 frame to H.264 and assert the output
is a valid Annex-B stream (reuse `ffmpeg_spike` pattern) using only the public API.

### Implementation for User Story 2

- [x] T014 [P] [US2] Implement `FFmpegVideoEncoder` (libavcodec/libx264, `Init`/`Encode`/
      `Flush`/`Release`, SPS/PPS tagging, `keyframe` + `pts_us` on `Packet`) in
      `codec/src/framework/backend/ffmpeg/video_encoder.cc` / `.h` per
      `contracts/backend-contract.md`.
- [x] T015 [P] [US2] Implement `FFmpegAudioEncoder` (AAC via `libavcodec`) in
      `codec/src/framework/backend/ffmpeg/audio_encoder.cc` / `.h`.
- [x] T016 [US2] Register both behind the factory `select()` via
      `codec/src/framework/backend/ffmpeg/register.cc` (deps
      `//third_party/ffmpeg:ffmpeg_codec`; ADR-001 force-loaded archive).
- [ ] T017 [US2] Add integration test encoding a frame → valid H.264 in
      `codec/tests/backend/ffmpeg/encode_test.cc` **plus its `BUILD.bazel` package**
      (`codec/tests/backend/ffmpeg/BUILD.bazel`). Validates MVP: pull API returns
      encodable packets without the ring buffer. **Currently MISSING** — MVP is
      unverified by a test.

**Checkpoint**: `bazel test //src/framework/backend/ffmpeg/...` passes — a valid
H.264 stream is produced through the public API. MVP achieved. ⚠️ pending T017.

---

## Phase 5: User Story 3 — Output ring buffer (Priority: P2)

**Goal**: The encoder→consumer transport you designed: a bounded SPSC lock-free ring
buffer (`PacketQueue`) implementing `PacketSink` (producer) and
`PacketSource` (consumer), with configurable back-pressure (default `kBlock`).

**Independent Test**: Producer pushes N packets, consumer drains with `Pull(deadline)`
and receives all N in order with zero loss under `kBlock`; verify power-of-two
capacity + blocking back-pressure in unit tests.

### Implementation for User Story 3

- [x] T018 [P] [US3] Declare `Backpressure` enum, `Status`, `PacketSink`,
      `PacketSource` interfaces in `codec/src/framework/queue/queue_iface.h`
      per `contracts/output-queue-contract.md`.
- [x] T019 [US3] Implement `PacketQueue` (fixed power-of-two `slots[]`, atomic
      `head_`/`tail_`, move-in/move-out, `Push` honoring `Backpressure`) in
      `codec/src/framework/queue/packet_queue.h` / `.cc`; constructor
      `PacketQueue(size_t capacity, Backpressure policy = Backpressure::kBlock)`
      per ADR-005 / `output-queue.md`.
- [x] T020 [US3] Wire the encoder's optional `PacketSink` push mode: add an
      `PacketSink*` member to the encoder base and,
      after a successful `Encode()`, forward `Packet&&` to it (pull API still
      the default; push is opt-in) per `data-model.md` §8. **Done in spec 004
      (`004-encoder-queue-wiring`)**: `SetOutputSink` on the abstract encoders (default
      `kUnsupportedOperation`), FFmpeg video/audio backends push produced packets to the
      sink, caller owns `MarkEos`. Validated end-to-end with a real encoder.
- [x] T021 [US3] Add ring-buffer unit tests in `codec/tests/queue/packet_queue_test.cc`:
      SPSC correctness, power-of-two masking, `kBlock` blocks, `kLatest` overwrites,
      `kError` returns back-pressure code (contract *Acceptance*).

**Checkpoint**: Ring buffer is independently testable; encoder can push packets into it
with no loss under `kBlock`. ⚠️ push path pending T020.

---

## Phase 6: User Story 4 — Consumer (PacketConsumer / PacketSource::Await / FileSink) (Priority: P2)

**Goal**: The consumer side of the ring buffer: a `PacketSource::Await` drain loop pops from
`PacketSource` and forwards to a `PacketConsumer`. Ship `FileSinkConsumer`
(write `.h264`/`.aac` or mux `.mp4`); define `StreamConsumer` contract (RTMP/SRT/WebRTC)
as deferred-but-designed.

**Independent Test**: End-to-end — encoder → `PacketQueue` → `PacketSource::Await` →
`FileSinkConsumer` writes a valid `.h264` with correct order and no packet loss under
`kBlock`; swapping in a stub `StreamConsumer` requires no encoder change.

### Implementation for User Story 4

- [x] T022 [P] [US4] Declare `PacketConsumer` interface (`Push(VideoPacket&&)`,
      `Push(VideoPacket&&)`, `Flush`, `Finish`) and `PacketSource::Await::Run` helper in
      `codec/src/framework/consumer/packet_consumer.h` / `queue/queue_iface.h` per
      `contracts/output-queue-contract.md` + `output-queue.md` §3.
- [x] T023 [US4] Implement `FileSinkConsumer` (Annex-B → `.h264` / `.aac`; preserve
      order + keyframe/SPS-PPS at segment start; flush/close on `Finish()`) in
      `codec/src/framework/consumer/file_sink_consumer.cc` / `.h`.
- [x] T024 [US4] Add the end-to-end drain test in
      `codec/tests/consumer/file_sink_consumer_test.cc`: `PacketSource::Await` → `FileSinkConsumer`
      produces a valid file, in order, zero loss (contract *Acceptance*). Asserts swapping
      `FileSinkConsumer` for a stub `StreamConsumer` needs no encoder change. **Note**:
      the producer is synthetic (fake `Packet`s), not a real encoder — it proves
      the transport/consumer, not the encoder→queue wiring (that needs T020).
- [ ] T025 [US4] Add `StreamConsumer` abstract contract header (RTMP/SRT/WebRTC
      deferred per `project_bootstrap.md`) in `codec/src/framework/consumer/stream_consumer.h`
      documenting framing / connection lifecycle / pacing / back-pressure obligations
      from `output-queue.md` §4.2 (no network client yet).

**Checkpoint**: The full transport works end-to-end to a file; the encoder is
transport-agnostic (file ↔ stream is a one-line `PacketConsumer` swap). ⚠️ T025 pending.

---

## Phase 7: User Story 5 — Public umbrella, example, verification (Priority: P3)

**Goal**: Expose the library through `public`, ship a runnable encode-to-file example,
and wire the `make`-based validation + CI matrix.

**Independent Test**: `make verify` (or the CI job) builds + runs the example that
encodes a clip to `.h264` via `queue` + `FileSinkConsumer` and asserts the file is valid.

### Implementation for User Story 5

- [ ] T026 [US5] Finalize public umbrella header + `VIDEO_CODEC_API` export in
      `codec/src/framework/public/include/video_codec/video_codec.h` per
      `contracts/public-api.md`; only `video_codec_export.h` exists today. Keep `public`
      as the only `//visibility:public` module.
- [ ] T027 [US5] Add example `codec/src/examples/ffmpeg_encode_file.cc` that wires
      `FFmpegVideoEncoder` → `PacketQueue` → `PacketSource::Await` → `FileSinkConsumer`.
- [ ] T028 [US5] Register the example + a smoke target in `codec/src/examples/BUILD.bazel`
      and add it to `codec/mk/` + `scripts/verify/` categorized validation (per spec 001
      mechanism).
- [ ] T029 [US5] Confirm CI matrix (`codec/.github/workflows/ci.yml`, `android.yml`)
      builds + tests on macOS ARM64 / Linux x86_64 / Android cross-build; add the
      example/smoke job if missing. **Currently MISSING** — no `.github/workflows/` yet.

**Checkpoint**: A consumer can `ffmpeg_encode_file` through the public API and the ring
buffer; CI is green on the target matrix.

---

## Phase N: Polish & Cross-Cutting Concerns

**Purpose**: Consistency audit (the kind we just did for back-pressure), docs, release.

- [ ] T030 [P] Run a cross-doc consistency audit: back-pressure default (`kBlock`) agrees
      across `research.md` R9, `data-model.md`, `output-queue.md`, ADR-005,
      `output-queue-contract.md`; module graph in `plan.md` matches
      `module-dependencies.md` (incl. `consumer` node and the `utils` module).
- [ ] T031 [P] Update `CHANGELOG.md` "Unreleased" with the implemented modules and the
      ring-buffer + consumer transport.
- [ ] T032 Run `codec/doc/quickstart.md` validation end-to-end and fix any drift between
      docs and the implemented BUILD/test layout.
- [ ] T033 Optional: author `StreamConsumer` network clients (RTMP/SRT/WebRTC) as a
      follow-on spec once `FileSinkConsumer` + `PacketSource::Await` are proven.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No deps — start immediately. ✅ done.
- **Foundational (Phase 2)**: Depends on Setup — BLOCKS all stories. ✅ partially (T008 utils pending).
- **US1 (Phase 3)**: Depends on Foundational. ✅ done.
- **US2 (Phase 4, MVP)**: Depends on Foundational + US1 (factory returns the backend). ⚠️ impl done, T017 test pending.
- **US3 (Phase 5)**: Depends on Foundational (core types) — independent of US1/US2. ✅ impl done, T020 wiring pending.
- **US4 (Phase 6)**: Depends on US3 (queue) + US1 (`PacketConsumer` consumes
  `Packet` from `api`/`core`); can start once US3 lands. ✅ done (T025 pending).
- **US5 (Phase 7)**: Depends on US2 + US4 (needs a real backend + consumer to wire the
  example). ⚠️ all pending.
- **Polish (Phase N)**: Depends on all desired stories.

### User Story Dependencies

- **US1 (P1)**: After Foundational — no dependency on other stories. ✅ done.
- **US2 (P1, MVP)**: After Foundational + US1 — may integrate with US1 only. ⚠️ T017 pending.
- **US3 (P2)**: After Foundational — independent of US1/US2; pairs with US4. ✅ impl, T020 pending.
- **US4 (P2)**: After US3 + US1. ✅ impl, T025 pending.
- **US5 (P3)**: After US2 + US4. ⚠️ all pending.
- **T008 (utils)**: Foundational — land in Phase 2, before US2/US5.

### Within Each User Story

- Interfaces/contracts before implementation.
- Core types (Phase 2) before any story.
- Implementation before its test/acceptance task.
- Story complete before the next priority.

### Parallel Opportunities

- All Setup tasks marked [P] (T001/T002/T003) can run in parallel. ✅ done.
- All Foundational tasks marked [P] (T004–T008) can run in parallel within Phase 2.
- US3 (ring buffer) and US1 (encoder surface) can be developed in parallel after
  Foundational — they share only `core` types. ✅ done.
- Within a story, interface declaration [P] tasks precede the implementation task.

---

## Parallel Example: Phase 5 (Output ring buffer) + Phase 3 (Encoder surface)

```bash
# After Foundational (Phase 2) completes:
# US3 — ring buffer (independent of encoder surface):
Task: "Declare Backpressure/Status/PacketSink/PacketSource in queue/queue_iface.h"
Task: "Implement PacketQueue SPSC in queue/packet_queue.{h,cc}"
# US1 — encoder surface (independent of ring buffer):
Task: "Declare VideoEncoder/AudioEncoder abstract in api/video_encoder.h"
Task: "Implement lifecycle state machine in api/encoder_lifecycle.{h,cc}"
```

---

## Implementation Strategy

### MVP First (US2 only — real encoding)

1. Complete Phase 1 (Setup) + Phase 2 (Foundational). ✅
2. Complete Phase 3 (US1 encoder surface) + Phase 4 (US2 FFmpeg backend). ✅ impl, ⚠️ T017 test.
3. **STOP and VALIDATE**: `encode_test.cc` (T017) produces a valid H.264 stream via the pull API. ← **not yet done**.
4. Demo: encode to in-memory packets works.

### Incremental Delivery (add the transport you designed)

1. Setup + Foundational → foundation ready. ✅ (T008 utils still pending)
2. Add US1 + US2 → MVP (encode to packets). **Demo.** ⚠️ T017 test pending.
3. Add US3 (ring buffer) → encoder can push into a bounded SPSC queue. **Demo needs T020.** ✅ queue, ⚠️ wiring.
4. Add US4 (consumer) → encoder → queue → `FileSinkConsumer` writes a valid file,
   transport-agnostic. **Demo** (T024 tests the consumer; real encoder→queue needs T020).
5. Add US5 (public + example + CI) → `ffmpeg_encode_file` example + green CI matrix. ⚠️ all pending.
6. Polish (Phase N) → consistency audit, CHANGELOG, quickstart validation. ⚠️ pending.

### Parallel Team Strategy

- Developer A: Phase 2 Foundational (incl. T008 utils), then US1 (encoder surface).
- Developer B (after Foundational): US3 (ring buffer) in parallel with A's US1.
- Developer C (after US3 + US1): US4 (consumer), then US5 (public/example).
- US2 (FFmpeg backend) is the MVP critical path — staff early; close T017 test next.

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
- **Reconciliation note (2026-08-12)**: tasks T001–T006, T009–T016, T018–T019,
  T021–T024 were implemented in commit `628e5be` and are marked `[x]` here. Genuine
  remaining gaps are T008 (utils), T017 (backend MVP test), T020 (encoder→queue
  wiring), T025 (StreamConsumer header), T026–T029 (public/example/CI), T030–T033
  (polish).
