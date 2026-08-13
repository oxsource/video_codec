# Tasks: Core Utilities & Public API Surface (spec 003)

**Input**: Design documents from `specs/003-core-utils-public-api/`
(`plan.md`, `spec.md`, `research.md`, `data-model.md`, `contracts/public-api.md`, `quickstart.md`)

**Prerequisites**: spec `001-project-scaffold` (Bazel 6.5.0 workspace, C++17, `core` types
already implemented in commit `628e5be`). The `utils` and `public` Bazel packages already
exist as stubs; this list fills them in.

**Tests**: Included — the spec's Success Criteria (SC-001..SC-004) and the project testing
strategy require unit tests (conversion round-trip, stride references, PCM, and a
header-only public compile test). Tests are written first and must FAIL before implementation.

**Organization**: Tasks are grouped by user story so each can be built and verified
independently. Both stories are P1 and depend only on the already-present `core`
foundation, so they can run in parallel.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Package scaffolding)

**Purpose**: Create the one missing Bazel package (the `utils` test package) and confirm the
existing `utils` / `public` stubs expose the right visibility for this feature.

- [x] T001 Create the `utils` test package `codec/tests/utils/BUILD.bazel` with
      `package(default_visibility = ["//src/framework:__subpackages__", "//tests:__subpackages__"])`
      and a `cc_test` target wired to googletest, so the conversion/stride/PCM tests have a home.
- [x] T002 [P] Verify (no change expected) that `codec/src/framework/utils/BUILD.bazel` globs
      `*.cc`/`*.h` and deps only `//src/framework/core`, and that
      `codec/src/framework/public/BUILD.bazel` globs `include/video_codec/*.h` and is
      `//visibility:public`. Document confirmation in the task.

**Checkpoint**: `bazel build //src/framework/...` resolves; the `tests/utils` package exists
and is visible inside the framework; `public` remains the only public-visibility module.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The only prerequisite — the `core` types/error model (`VideoFrame`, `AudioFrame`,
`Packet`, `PixelFormat`, `SampleFormat`, `StatusCode`, `Result<T>`) — is already
implemented (commit `628e5be`). Enforce the architecture rule that `utils` must stay
`core`-only.

- [x] T003 [P] Enforce the `utils`-only-`core` dependency rule: confirm
      `codec/src/framework/utils/BUILD.bazel` has NO dependency on `api`, `backend/*`,
      `queue`, or `consumer` (only `//src/framework/core`). If any leaked in, remove it. This
      guards the architecture constraint from `module-dependencies.md`.

**Checkpoint**: Foundation ready — `core` is present and `utils` is provably backend-free.
User stories can now begin in parallel.

---

## Phase 3: User Story 1 - Core media utilities (Priority: P1)

**Goal**: A dependency-free `utils` module providing pixel-format conversion (kI420↔kNV12),
stride computation, and PCM sample-format conversion, used by the FFmpeg/AAC backend and the
future example.

**Independent Test**: `bazel test //tests/utils/...` passes — every supported conversion
round-trips bit-exactly (or within documented tolerance), stride matches hand-computed
references, and unsupported conversions return an error status rather than corrupt output.

### Tests for User Story 1 (write FIRST, ensure they FAIL)

- [x] T004 [P] [US1] Add YUV conversion round-trip test in
      `codec/tests/utils/yuv_convert_test.cc`: `kI420 → kNV12 → kI420` is bit-exact on a
      reference buffer; an unsupported format pair returns an error `StatusCode`.
- [x] T005 [P] [US1] Add stride reference test in `codec/tests/utils/stride_test.cc`:
      `RowStride(width, PixelFormat)` matches expected byte layouts for representative
      widths/formats; `width == 0` returns an error status.
- [x] T006 [P] [US1] Add PCM conversion test in `codec/tests/utils/pcm_convert_test.cc`:
      `kS16 ↔ kF32Planar` (and other supported pairs) round-trip within tolerance; an
      unsupported pair returns an error status.

### Implementation for User Story 1

- [x] T007 [P] [US1] Implement `yuv_convert.h` / `yuv_convert.cc` in
      `codec/src/framework/utils/` — `ConvertPixelFormat(VideoFrame, PixelFormat::kNV12, ...)`
      doing planar YUV420P ↔ semi-planar NV12 with explicit per-plane strides; returns
      `StatusCode`.
- [x] T008 [P] [US1] Implement `stride.h` / `stride.cc` in `codec/src/framework/utils/` —
      `RowStride(width, PixelFormat)` and `SampleStride(...)` returning padded byte counts;
      returns error status for invalid input.
- [x] T009 [P] [US1] Implement `pcm_convert.h` / `pcm_convert.cc` in
      `codec/src/framework/utils/` — `ConvertSampleFormat(...)` among `kS16`/`kF32` ×
      interleaved/planar; returns `StatusCode`.

**Checkpoint**: `bazel test //tests/utils/...` is green; `utils` is independently usable and
`core`-only.

---

## Phase 4: User Story 2 - Public umbrella header & API export (Priority: P1)

**Goal**: A single public include surface (`video_codec.h`) re-exporting the frozen public
contracts, plus reuse of the existing `VIDEO_CODEC_API` export macro, so consumers compile
against one header and the library links static or shared.

**Independent Test**: A header-only compile test that `#include`s only `video_codec.h`, calls
a public factory/encoder entry point, and links for both static and shared builds using
exclusively public symbols (no internal header reachable).

### Tests for User Story 2 (write FIRST, ensure they FAIL)

- [x] T010 [P] [US2] Add header-only public compile/link test in
      `codec/tests/public/umbrella_compile_test.cc` (+ `codec/tests/public/BUILD.bazel`):
      include only `<video_codec/video_codec.h>`, reference `VideoEncoderFactory` /
      `VideoEncoder` / core types / `LogSlot`, and assert the target builds and links. This
      covers contract acceptance A1/A2.

### Implementation for User Story 2

- [x] T011 [US2] Create the umbrella header
      `codec/src/framework/public/include/video_codec/video_codec.h` that `#include`s the
      public contracts — `core/types.h`, `core/status.h`, `core/result.h`, `core/log_slot.h`,
      `api/video_encoder.h`, `api/audio_encoder.h`, `api/input_surface.h`,
      `api/encoder_factory.h` — and decorates the surface with `VIDEO_CODEC_API` (from the
      existing `video_codec_export.h`). No new types; no `BUILD.bazel` change needed (already
      globs `*.h`, `//visibility:public`).

**Checkpoint**: A consumer compiles against the library via `video_codec.h` alone; public
surface re-exports exactly the intended contracts and nothing internal.

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: Validate the quickstart end-to-end and record the addition.

- [x] T012 [P] Run `quickstart.md` validation: `bazel build //src/framework/...` and
      `bazel test //tests/utils/... //tests/public/...` all pass; the snippet in
      `quickstart.md` compiles.
- [x] T013 [P] Update `CHANGELOG.md` "Unreleased" with the `utils` module and the public
      umbrella/export surface added by this feature.
- [x] T014 [P] Confirm `contracts/public-api.md` matches the final umbrella (re-export list
      and `VIDEO_CODEC_API` behavior); fix any drift.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No deps — start immediately.
- **Foundational (Phase 2)**: Depends on Setup; the `core` prerequisite is already present.
  Blocks both stories.
- **US1 (Phase 3)**: Depends on Foundational (core types). Independent of US2.
- **US2 (Phase 4)**: Depends on Foundational (core/api contracts exist). Independent of US1.
- **Polish (Phase 5)**: Depends on US1 + US2.

### User Story Dependencies

- **US1 (P1)**: After Foundational — no dependency on US2; can run in parallel with it.
- **US2 (P1)**: After Foundational — no dependency on US1; can run in parallel with it.

### Within Each User Story

- Tests (T004–T006, T010) written and FAILING before implementation (T007–T009, T011).
- Interface/helper implementation before its compile/link validation.
- Story complete before Polish.

### Parallel Opportunities

- All Setup tasks (T001, T002) can run in parallel.
- Foundational T003 is independent.
- **US1 and US2 can be developed fully in parallel** after Foundational — they share only
  `core` and touch different packages (`utils` vs `public`).
- Within US1, the three conversion areas (T007/T008/T009) and their tests (T004/T005/T006)
  are parallelizable (different files).
- T012–T014 (Polish) are parallelizable.

---

## Parallel Example: US1 + US2 (after Foundational)

```bash
# Developer A — utils (US1):
Task: "Add yuv_convert_test.cc (T004)"
Task: "Implement yuv_convert.h/.cc (T007)"
Task: "Add stride_test.cc (T005)"
Task: "Implement stride.h/.cc (T008)"
Task: "Add pcm_convert_test.cc (T006)"
Task: "Implement pcm_convert.h/.cc (T009)"

# Developer B — public umbrella (US2), in parallel:
Task: "Add umbrella_compile_test.cc (T010)"
Task: "Create video_codec.h umbrella (T011)"
```

---

## Implementation Strategy

### MVP First

This feature is small and both stories are P1. The library is not consumable without the
public surface, so deliver **US1 + US2 together** as the MVP increment:

1. Setup (T001–T002) + Foundational (T003).
2. US1 (T004–T009): `utils` module + tests → `bazel test //tests/utils/...` green.
3. US2 (T010–T011): umbrella header + compile test → consumer builds against one header.
4. **STOP and VALIDATE**: `bazel build //src/framework/...` and the two test packages pass;
   quickstart snippet compiles.
5. Polish (T012–T014), then demo.

### Incremental Delivery

1. Setup + Foundational → foundation ready.
2. US1 → `utils` usable by backends/examples (tested). **Demo.**
3. US2 → library consumable via one header (tested). **Demo (MVP complete).**
4. Polish → quickstart validation, CHANGELOG, contract sync.

### Parallel Team Strategy

- Developer A: Setup + Foundational, then US1 (utils).
- Developer B: US2 (public umbrella) in parallel with A's US1.
- Both converge at Polish.

---

## Notes

- [P] tasks = different files, no dependencies.
- [Story] label maps a task to its capability cluster for traceability.
- Each story is independently completable and testable.
- Tests are written first and must fail before implementation (TDD per project strategy).
- Commit after each task or logical group.
- This plan deliberately does NOT re-implement core types/error/`LogSlot`/encoder interfaces
  (already shipped in `628e5be`); it adds only `utils` (T008 in spec-002 list) and the public
  umbrella (T026 in spec-002 list).
