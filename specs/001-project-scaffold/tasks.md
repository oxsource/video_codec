# Tasks: video_codec Project Scaffold

**Input**: Design documents from `specs/001-project-scaffold/`

**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, contracts/, quickstart.md

**Tests**: OPTIONAL — not explicitly requested in spec.md. Only the spike executables
and an empty `tests/` target are created in this scaffold phase; no unit-test tasks
are included. (Real encoder unit tests are deferred to Phase 2 implementation.)

**Organization**: Tasks grouped by phase. User-story phases (US1, US2) are each an
independently testable increment. Workspace root is `codec/`.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: User story this task belongs to (US1, US2)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Initialize the Bazel workspace root at `codec/` and global build config.

- [X] T001 Create `codec/WORKSPACE` with `workspace(name = "video_codec")` and `load("//:video_codec_deps.bzl", "video_codec_setup")` + `video_codec_setup()`
- [X] T002 [P] Create `codec/.bazelversion` containing `6.5.0`
- [X] T003 [P] Create `codec/.bazelrc` (C++17 cxxopts, visibility=hidden, platform aliases `android_arm64`/`linux_x86_64`/`darwin_arm64`, default `linux_x86_64_platform`, `test --test_output=errors`)
- [X] T004 [P] Create `codec/.bazelignore` (ignore example/non-workspace dirs)
- [X] T005 Create `codec/BUILD.bazel` root alias `//:video_codec` → `//src/framework/public:video_codec` with `//visibility:public`
- [X] T006 Create `codec/video_codec_deps.bzl` with `video_codec_setup()` declaring FFmpeg 6.1, googletest 1.14.0, bazel_skylib 1.6.1, each guarded by `native.existing_rule()`

**Checkpoint**: Workspace evaluates; `bazel version` matches 6.5.0.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Platform defs, third-party wrappers, and all module `BUILD.bazel`
stubs. NO user-story work can begin until this phase is complete.

- [ ] T007 [P] Create `codec/platforms/platforms.bzl` with `config_setting_and_platform` macro and `video_codec_select()`
- [ ] T008 [P] Create `codec/platforms/BUILD` defining `android_arm64`, `linux_x86_64`, `darwin_arm64` (os+cpu constraint_values)
- [ ] T009 [P] Create `codec/third_party/ffmpeg/BUILD.bazel` wrapper (`ffmpeg_codec` cc_library over libavcodec/libavutil; **enable libx264/libx265 encoders**; platform `linkopts` only on matching platform)
- [ ] T010 [P] Create `codec/third_party/android_ndk/BUILD.bazel` wrapper exposing `media/NdkMediaCodec.h` for host-referenced headers
- [ ] T011 [P] Create `codec/src/framework/core/BUILD.bazel` empty `cc_library` stub (visibility `__subpackages__`, `tests`)
- [ ] T012 [P] Create `codec/src/framework/api/BUILD.bazel` empty `cc_library` stub (deps core)
- [ ] T013 [P] Create `codec/src/framework/utils/BUILD.bazel` empty `cc_library` stub (deps core)
- [ ] T014 [P] Create `codec/src/framework/backend/android/BUILD.bazel` empty `cc_library` stub (deps core, api, utils, `@androidndk`)
- [ ] T015 [P] Create `codec/src/framework/backend/ffmpeg/BUILD.bazel` empty `cc_library` stub (deps core, api, utils, `@ffmpeg`)
- [ ] T016 Create `codec/src/framework/public/BUILD.bazel` umbrella `cc_library` aggregating core/api/utils + `select()` backend, plus `include/video_codec/video_codec_export.h` defining `VIDEO_CODEC_API` (visibility `//visibility:public`)
- [ ] T017 [P] Create `codec/tests/BUILD.bazel` empty `cc_test`/test target placeholder (test infra present)

**Checkpoint**: `bazel build //...` succeeds on host (Linux/macOS); `@ffmpeg` reachable only via `backend/ffmpeg`, NDK only via `backend/android`.

---

## Phase 3: User Story 1 - FFmpeg Encode Spike (Priority: P1) 🎯 MVP

**Goal**: Prove the highest-risk dependency (FFmpeg `libavcodec`) compiles, links,
and produces a valid H.264 bitstream from a synthetic NV12 frame on the dev host.

**Independent Test**: `bazel run //src/spike:ffmpeg_spike` writes a non-empty
`.h264`; `ffprobe`/`ffplay` decodes it successfully.

- [ ] T018 [US1] Implement `codec/src/spike/ffmpeg_spike.cc`: allocate `AVCodecContext` for `libx264`, build a synthetic NV12 `AVFrame`, encode to `AVPacket`, write Annex-B bytes to `ffmpeg_spike.h264`
- [ ] T019 [US1] Add `ffmpeg_spike` `cc_binary` target to `codec/src/spike/BUILD.bazel` (deps `@ffmpeg//:ffmpeg_codec`, `//src/framework/core`)
- [ ] T020 [US1] Run `bazel run //src/spike:ffmpeg_spike` and verify output `.h264` is a valid, decodable H.264 stream; adjust FFmpeg wrapper/config until it passes

**Checkpoint**: FFmpeg encode pipeline validated end-to-end on host. User Story 1 independently functional.

---

## Phase 4: User Story 2 - MediaCodec Build Spike (Priority: P2)

**Goal**: Prove Android NDK MediaCodec wiring compiles under the Android toolchain
without breaking the host build.

**Independent Test**: `bazel build //src/spike:mediacodec_spike --platforms=//platforms:android_arm64_platform` succeeds; host `bazel build //...` still succeeds without NDK present.

- [ ] T021 [US2] Implement `codec/src/spike/mediacodec_spike.cc`: configure `AMediaCodec` for `video/avc`, queue one input buffer, drain output (guarded by `#if defined(__ANDROID__)`)
- [ ] T022 [US2] Add `mediacodec_spike` `cc_binary` to `codec/src/spike/BUILD.bazel` with `target_compatible_with = ["@platforms//os:android"]` and `@androidndk` dep
- [ ] T023 [US2] Verify Android build succeeds under NDK toolchain AND host `bazel build //...` excludes NDK (no `target_compatible_with` bleed)

**Checkpoint**: MediaCodec path compiles; host build remains NDK-free. User Story 2 independently functional.

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: Validate the whole scaffold and align docs.

- [ ] T024 [P] Run `bazel build //...` and `bazel test //...` on host (Linux x86_64 / macOS ARM64) and confirm clean
- [ ] T025 [P] Execute `specs/001-project-scaffold/quickstart.md` steps; confirm `ffmpeg_spike` output validity
- [ ] T026 Documentation: confirm `codec/doc/project_bootstrap.md` and `specs/001-project-scaffold/quickstart.md` match the produced layout; note `examples/` and `backend/darwin` deferred to Phase 2

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately
- **Foundational (Phase 2)**: Depends on Setup — BLOCKS all user stories
- **US1 (Phase 3)**: Depends on Foundational
- **US2 (Phase 4)**: Depends on Foundational (independent of US1)
- **Polish (Phase 5)**: Depends on US1 + US2

### User Story Dependencies

- **US1 (P1, MVP)**: After Foundational — no dependency on US2
- **US2 (P2)**: After Foundational — independent of US1; can run in parallel if staffed

### Within Each User Story

- Implement spike source before its `BUILD.bazel` target registration
- Register target before running/verifying
- Story complete before Polish

### Parallel Opportunities

- All Phase 1 tasks T002–T004 are parallel (different files)
- All Phase 2 tasks T007–T017 are parallel (independent `BUILD.bazel` / `.bzl` files)
- US1 and US2 can be developed in parallel by different developers after Foundational
- Within a story, source file (T018/T021) and BUILD target (T019/T022) touch different
  files but T019/T022 depend on the source existing — do source first

---

## Parallel Example: Foundational Phase (Phase 2)

```bash
# Launch all independent BUILD/`.bzl` scaffolding tasks together:
Task: "T007 platforms/platforms.bzl"
Task: "T008 platforms/BUILD"
Task: "T009 third_party/ffmpeg/BUILD.bazel"
Task: "T010 third_party/android_ndk/BUILD.bazel"
Task: "T011-T015 module BUILD.bazel stubs (core/api/utils/backend/android/ffmpeg)"
Task: "T016 public/BUILD.bazel + video_codec_export.h"
Task: "T017 tests/BUILD.bazel"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL — blocks all stories)
3. Complete Phase 3: User Story 1 (ffmpeg_spike)
4. **STOP and VALIDATE**: run spike, confirm valid `.h264`
5. Scaffold MVP is proven — FFmpeg backend path is real

### Incremental Delivery

1. Setup + Foundational → foundation ready
2. Add US1 (ffmpeg_spike) → test on host → MVP proven
3. Add US2 (mediacodec_spike) → Android build validated
4. Polish → full scaffold validated

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to US1/US2 for traceability
- `backend/darwin` (VideoToolbox) and `examples/` encode demos are intentionally
  NOT in this scaffold phase — deferred to Phase 2 implementation
- FFmpeg wrapper MUST enable libx264/libx265 encoders (not just decode) for the spike
