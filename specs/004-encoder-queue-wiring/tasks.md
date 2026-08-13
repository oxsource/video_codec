# Tasks: Encoder-to-Queue Push Wiring (spec 004)

**Input**: Design documents from `specs/004-encoder-queue-wiring/`
(`plan.md`, `spec.md`, `research.md`, `data-model.md`, `contracts/push-mode-contract.md`, `quickstart.md`)

**Prerequisites**: spec 002 implementation (encoder framework, `PacketQueue`,
`PacketSource::Await`, `FileSinkConsumer` shipped) and spec 003 (`utils`, public umbrella). The queue
producer endpoint is `OutputSink`; consumer endpoint is `PacketSource` — both exist.

**Tests**: Included — the spec's acceptance criteria (zero-loss push, pull unchanged, flush +
EOS, back-pressure) and the project testing strategy require them. Tests are written first
and must FAIL before implementation.

**Organization**: Two user stories. US1 (P1) is the push wiring; US2 (P2) verifies
back-pressure pacing. Both depend on the api hook + backend impl tasks.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Test package)

**Purpose**: Create the one missing test package (backend/ffmpeg integration tests) and add
the build dependency the push implementation needs.

- [x] T001 [P] Create `codec/tests/backend/ffmpeg/BUILD.bazel` with `cc_test` targets for
      `encode_push_test` and `audio_push_test`, depending on
      `//src/framework/backend/ffmpeg`, `//src/framework/queue`, `//src/framework/core`,
      `//src/framework/api`, and `@com_google_googletest//:gtest_main`.
- [x] T002 Add `//src/framework/queue` to `deps` in
      `codec/src/framework/backend/ffmpeg/BUILD.bazel` (backends must include
      `queue/queue_iface.h` for `OutputSink`).

**Checkpoint**: The ffmpeg backend compiles with the queue dep; the test package resolves.

---

## Phase 2: Foundational (Contract hook on the encoder surface)

**Purpose**: The `SetOutputSink` hook that all backends and tests build on. `api` stays
`queue`-free (forward-declared `OutputSink`).

- [x] T003 [P] Add `virtual Status SetOutputSink(OutputSink* sink)` to
      `codec/src/framework/api/video_encoder.h` and
      `codec/src/framework/api/audio_encoder.h`, with `class OutputSink;` forward declaration
      and a default `return Status::kUnsupportedOperation;`. No `queue` include in `api`.

**Checkpoint**: `api` compiles; a non-overriding subclass sees `kUnsupportedOperation`.

---

## Phase 3: User Story 1 - Encoder pushes encoded output to a queue (Priority: P1)

**Goal**: When a sink is attached, every packet from a successful `Encode()`/`Flush()` is
moved into the sink; pull API stays the default and unchanged.

**Independent Test**: A real FFmpeg video encoder → `PacketQueue(kBlock)` → drain
produces all N packets in order with zero loss; with no sink, pull still returns full packets.

### Tests for User Story 1 (write FIRST, ensure they FAIL)

- [x] T004 [P] [US1] Add api contract test in
      `codec/tests/api/encoder_push_contract_test.cc`: a stub `VideoEncoder` subclass that
      does NOT override `SetOutputSink` returns `Status::kUnsupportedOperation` (default
      contract, per push-mode-contract A4).
- [x] T005 [P] [US1] Add real-encoder push test in
      `codec/tests/backend/ffmpeg/encode_push_test.cc`: attach `PacketQueue(kBlock)`
      to a real FFmpeg video encoder, encode N frames, drain and assert order + zero loss;
      assert `Encode()` returns `kOk` with an empty packet in push mode (A1); with no sink,
      assert `Encode()` returns the full packet (A2); flush then caller `MarkEos()` → drain
      observes `kEos` (A3).
- [x] T006 [P] [US1] Add audio push test in
      `codec/tests/backend/ffmpeg/audio_push_test.cc`: attach the queue to a real FFmpeg AAC
      encoder, encode audio frames, assert audio packets (`PacketType::kAudio`) arrive at the
      sink in order (A5).

### Implementation for User Story 1

- [x] T007 [US1] Implement push in `codec/src/framework/backend/ffmpeg/video_encoder.h` /
      `.cc`: override `SetOutputSink` to store a non-owning `OutputSink* sink_`; in `Drain()`
      and `Flush()`, when `sink_` is set, move the produced `Packet&&` into
      `sink_->Submit(...)` and return an empty packet with `kOk`; on `Release()` set
      `sink_ = nullptr`.
- [x] T008 [US1] Implement push in `codec/src/framework/backend/ffmpeg/audio_encoder.h` /
      `.cc`: same pattern for audio via `OutputSink::Submit(Packet&&)`.

**Checkpoint**: `bazel test //tests/backend/ffmpeg/...` passes — real encoder → queue → drain
is order-preserving and lossless; pull mode is unchanged.

---

## Phase 4: User Story 2 - Queue back-pressure governs encode pacing (Priority: P2)

**Goal**: A slow consumer fills the queue and the producer blocks (default `kBlock`), pacing
the encoder instead of dropping packets.

**Independent Test**: A slow consumer + bounded queue + real encoder: producer blocks on a
full queue, then after drain all packets arrive in order with zero loss.

### Implementation for User Story 2

- [x] T009 [US2] Add the back-pressure pacing test in
      `codec/tests/backend/ffmpeg/encode_push_test.cc`: slow consumer (drain with delay) over
      a small bounded `PacketQueue(kBlock)` fed by a real encoder; assert the producer
      blocks (test completes with zero loss) and all packets arrive in order after the
      consumer catches up (SC-003).

**Checkpoint**: Back-pressure end-to-end verified — slow consumer never loses packets; the
producer is naturally paced.

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: Validate the full build/test matrix and record the change.

- [x] T010 [P] Run quickstart validation: `bazel build //src/framework/...` and
      `bazel test //tests/...` all pass; the push-mode snippet in `quickstart.md` compiles.
- [x] T011 [P] Update `CHANGELOG.md` "Unreleased" with the encoder→queue push wiring.
- [x] T012 [P] Confirm `contracts/push-mode-contract.md` matches the implementation, and mark
      task **T020** as done (`[x]`) in `specs/002-architecture-engineering-design/tasks.md`.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No deps — start immediately.
- **Foundational (Phase 2)**: Depends on Setup (build dep) — BLOCKS US1/US2.
- **US1 (Phase 3)**: Depends on Foundational (T003).
- **US2 (Phase 4)**: Depends on US1 (needs a real encoder push path).
- **Polish (Phase 5)**: Depends on US1 + US2.

### User Story Dependencies

- **US1 (P1)**: After T002 + T003.
- **US2 (P2)**: After US1 — reuses the same push path with a slow consumer.

### Within Each User Story

- Tests (T004–T006) written and FAILING before implementation (T007–T008).
- Implementation before its integration/back-pressure test (T009).

### Parallel Opportunities

- T001 and T002 can run in parallel (different files).
- T003 is independent of the test files.
- T004/T005/T006 are parallelizable (different files).
- T007 and T008 are parallelizable (different files).
- T010–T012 (Polish) are parallelizable.

---

## Parallel Example: US1 (after T002 + T003)

```bash
# Developer A — video push:
Task: "Add encode_push_test.cc (T005)"
Task: "Implement push in video_encoder.{h,cc} (T007)"

# Developer B — audio push, in parallel:
Task: "Add audio_push_test.cc (T006)"
Task: "Implement push in audio_encoder.{h,cc} (T008)"

# Either — api contract test:
Task: "Add encoder_push_contract_test.cc (T004)"
```

---

## Implementation Strategy

### MVP First (US1)

1. Setup (T001–T002) + Foundational (T003).
2. US1 tests (T004–T006) — write, watch them FAIL.
3. US1 implementation (T007–T008) — real encoder → queue → drain green.
4. **STOP and VALIDATE**: `bazel test //tests/backend/ffmpeg/...` passes; pull unchanged.
5. US2 (T009) back-pressure test, then Polish (T010–T012).

### Incremental Delivery

1. Setup + Foundational → hook + build dep in place.
2. US1 → encoder pushes to queue (tested, zero loss). **Demo: real encoder → queue.**
3. US2 → back-pressure verified end-to-end. **Demo: slow-consumer safety.**
4. Polish → full build/test green, CHANGELOG, contract sync, spec-002 T020 closed.

### Parallel Team Strategy

- Developer A: Setup + Foundational, then US1 video path (T005 + T007).
- Developer B: US1 audio path (T006 + T008) in parallel.
- Developer C (after US1): US2 back-pressure test (T009) + Polish.

---

## Notes

- [P] tasks = different files, no dependencies.
- [Story] label maps a task to its capability cluster for traceability.
- Each story is independently completable and testable.
- Tests are written first and must fail before implementation (TDD per project strategy).
- This feature closes spec-002 task T020; no changes to `queue`, `consumer`, `public`, or
  the lifecycle state machine are in scope.
