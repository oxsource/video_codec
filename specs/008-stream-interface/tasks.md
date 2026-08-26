# Tasks: Stream Interface

**Input**: Design documents from `specs/008-stream-interface/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/stream-interface.md

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Stream Workspace)

**Purpose**: Initialize the `stream/` Bazel workspace parallel to `codec/`

- [x] T001 Create `stream/` directory structure and bootstrap files: `stream/WORKSPACE`, `stream/BUILD.bazel`, `stream/.bazelrc`, `stream/.bazelversion` (6.5.0)
- [x] T002 [P] Create `stream/video_stream_deps.bzl` with dependency bootstrap for googletest and bazel_skylib (following codec/video_codec_deps.bzl pattern)
- [x] T003 [P] Create `stream/Makefile` and `stream/mk/rules.mk` with module system (following codec/mk/ pattern)
- [x] T004 [P] Create `stream/.bazelignore` to exclude from codec workspace
- [x] T005 [P] Create `stream/src/api/BUILD.bazel` as placeholder for api library target
- [x] T006 [P] Create `stream/src/core/BUILD.bazel` as placeholder for core library target
- [x] T007 [P] Create `stream/tests/BUILD.bazel` as root test suite placeholder

---

## Phase 2: Foundational (Core Types & Interfaces)

**Purpose**: All user stories depend on these core types

- [x] T008 Create `stream/src/api/stream_config.h` with `video::stream::StreamConfig` struct per contracts/stream-interface.md
- [x] T009 Create `stream/src/api/stream_status.h` with `video::stream::StreamState` enum and `video::stream::StreamStatus` struct per contracts/stream-interface.md
- [x] T010 Create `stream/src/api/stream_backend.h` with `video::stream::StreamBackend` abstract interface per contracts/stream-interface.md
- [x] T011 Create `stream/src/api/stream.h` with `video::stream::Stream` abstract interface per contracts/stream-interface.md
- [x] T012 Create `stream/src/api/BUILD.bazel` with complete cc_library targets for stream.h, stream_backend.h, stream_config.h, stream_status.h

**Checkpoint**: Core types and interfaces ready — all user stories can now use these types

---

## Phase 3: User Story 1 — Push Media Stream via Unified Interface (Priority: P1) 🎯 MVP

**Goal**: Implement the core stream push flow: create stream, connect via WebRTC/WHIP, push encoded media, stop, and release

**Independent Test**: Push a pre-encoded H.264 stream to a WHIP-compatible endpoint and verify the remote side receives playable media

- [x] T013 [P] [US1] Create `stream/src/backend/mock/mock_backend.h` and `stream/src/backend/mock/mock_backend.cc` implementing `StreamBackend` for unit testing
- [x] T014 [P] [US1] Create `stream/src/backend/mock/BUILD.bazel` for mock backend library
- [x] T015 [US1] Create `stream/src/core/stream_impl.h` and `stream/src/core/stream_impl.cc` implementing `Stream` interface: lifecycle (Init, Start, Stop, Release), media input (SendVideo, SendAudio), status reporting, backend delegation
- [x] T016 [US1] Create `stream/src/core/stream_factory.cc` with `Stream::Create()` factory and backend registry lookup
- [x] T017 [US1] Create `stream/src/core/backend_registry.h` and `stream/src/core/backend_registry.cc` with `RegisterBackend()` and `CreateBackend()` functions
- [x] T018 [US1] Create `stream/src/core/BUILD.bazel` with complete cc_library targets for stream_impl, stream_factory, backend_registry
- [x] T019 [P] [US1] Add WebRTC dependency to `stream/video_stream_deps.bzl` (libwebrtc via rules_foreign_cc or system package)
- [x] T020 [US1] Create `stream/src/backend/webrtc/whip_session.h` and `stream/src/backend/webrtc/whip_session.cc` implementing WHIP signaling: POST to create, PATCH for ICE candidates, DELETE to teardown
- [x] T021 [US1] Create `stream/src/backend/webrtc/webrtc_backend.h` and `stream/src/backend/webrtc/webrtc_backend.cc` implementing `StreamBackend` interface using WebRTC peer connection and WHIP session
- [x] T022 [US1] Create `stream/src/backend/webrtc/BUILD.bazel` for webrtc backend library
- [x] T023 [US1] Create `stream/src/backend/webrtc/webrtc_raii.h` with RAII wrappers for WebRTC objects (following codec/src/framework/backend/ffmpeg/ffmpeg_raii.h pattern)
- [x] T024 [US1] Create `stream/tests/stream_interface_test.cc` with mock backend tests for lifecycle: Init, Start, Stop, Release, SendVideo, SendAudio
- [x] T025 [US1] Create `stream/tests/BUILD.bazel` with cc_test target for stream_interface_test

**Checkpoint**: US1 complete — basic stream push flow working with mock backend, WebRTC/WHIP backend ready

---

## Phase 4: User Story 2 — Backend Selection and Lifecycle Management (Priority: P2)

**Goal**: Backend plugin architecture with self-registration, lifecycle state machine validation, and error handling

**Independent Test**: Switch between valid WebRTC backend and unsupported backend, verify correct instantiation and error reporting

- [x] T026 [US2] Implement backend self-registration pattern in `stream/src/core/video_stream_register.h` with `VIDEO_STREAM_REGISTER` macro
- [x] T027 [US2] Add lifecycle state validation in `stream/src/core/stream_impl.cc`: reject invalid transitions per data-model.md state machine (e.g., Encode before Configured, Configure while Streaming)
- [x] T028 [US2] Add error reporting in `stream/src/core/stream_impl.cc`: populate `StreamStatus::last_error` and invoke `StatusCallback` on failures
- [x] T029 [US2] Create `stream/tests/backend_selection_test.cc` with tests for: valid backend instantiation, unsupported backend error, backend registration, lifecycle invalid transitions
- [x] T030 [US2] Update `stream/tests/BUILD.bazel` with cc_test target for backend_selection_test

**Checkpoint**: US2 complete — backend selection and lifecycle validation working

---

## Phase 5: User Story 4 — Run Test Server for Validation (Priority: P2)

**Goal**: Standalone WHIP test server binary with browser player page for end-to-end testing

**Independent Test**: Start the test server, push a stream via WebRTC backend, view the stream in a browser at http://localhost:<port>

- [x] T031 [P] [US4] Create `stream/src/test_server/whip_test_server.h` and `stream/src/test_server/whip_test_server.cc` with embedded HTTP server implementing WHIP endpoint: POST to create session, PATCH for ICE candidates, DELETE to teardown
- [x] T032 [P] [US4] Create `stream/src/test_server/web/player.html` with browser-based WebRTC player using `RTCPeerConnection` to subscribe and display live stream
- [x] T033 [US4] Create `stream/src/test_server/BUILD.bazel` with cc_binary target for test server, embedding the player.html as a resource
- [x] T034 [US4] Create `stream/test_server_main.cc` entry point parsing `--port` flag, initializing HTTP server, and serving WHIP endpoint + player page
- [x] T035 [US4] Create `stream/tests/whip_test_server_test.cc` with smoke test: start server, verify WHIP endpoint responds, verify player page serves
- [x] T036 [US4] Update `stream/tests/BUILD.bazel` with cc_test target for whip_test_server_test

**Checkpoint**: US4 complete — test server runs and browser can view pushed stream

---

## Phase 6: User Story 3 — Stream Configuration and Parameter Tuning (Priority: P3)

**Goal**: Unified configuration with sensible defaults, dynamic config updates, and validation

**Independent Test**: Provide different configurations and verify stream behavior matches expectations

- [x] T037 [US3] Add config validation in `stream/src/core/stream_impl.cc`: validate StreamConfig fields on Init/UpdateConfig, return error on invalid values
- [x] T038 [US3] Implement `UpdateConfig()` in `stream/src/core/stream_impl.cc` for runtime configuration changes
- [x] T039 [US3] Add adaptive bitrate (ABR) controller: `stream/src/core/abr_controller.h` and `stream/src/core/abr_controller.cc` — sample transport stats, adjust bitrate up/down based on packet loss and RTT thresholds per data-model.md
- [x] T040 [US3] Add auto-reconnect handler: `stream/src/core/reconnect_handler.h` and `stream/src/core/reconnect_handler.cc` — exponential backoff (1s, 2s, 4s, 8s, max 30s), ring buffer for local caching during disconnect, drain or flush on reconnect per data-model.md
- [x] T041 [US3] Add backpressure handling in `stream/src/core/stream_impl.cc`: configurable buffer limit, frame drop policy when transport cannot keep up
- [x] T042 [US3] Wire ABR controller and reconnect handler into `stream/src/core/stream_impl.cc` — ABR runs on stream thread, reconnect handler triggers on disconnect detection
- [x] T043 [US3] Update `stream/src/core/BUILD.bazel` with cc_library targets for abr_controller and reconnect_handler
- [x] T044 [US3] Create `stream/tests/abr_controller_test.cc` with tests for: bitrate increase on good network, decrease on packet loss, min/max clamping
- [x] T045 [US3] Create `stream/tests/reconnect_handler_test.cc` with tests for: exponential backoff timing, buffer recording during disconnect, buffer drain on reconnect
- [x] T046 [US3] Update `stream/tests/BUILD.bazel` with cc_test targets for abr_controller_test and reconnect_handler_test

**Checkpoint**: US3 complete — configuration, ABR, reconnect, and backpressure all working

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Build system hardening, CI, documentation, and quickstart validation

- [x] T047 [P] Create `stream/mk/build.mk` with build targets (`make stream-build`)
- [x] T048 [P] Create `stream/mk/test.mk` with test targets (`make stream-test`)
- [x] T049 [P] Create `stream/mk/help.mk` with help target (`make help`)
- [x] T050 [P] Add `.github/workflows/stream-ci.yml` — CI matrix for Linux x86_64 and macOS ARM64, build + test
- [x] T051 [P] Create `stream/.clang-format` matching codec's formatting conventions
- [x] T052 Update `stream/README.md` with build instructions, quickstart usage, and architecture overview
- [ ] T053 Run quickstart.md validation: verify `bazel build //src/api:stream_api` and `bazel test //tests/...` pass (requires codec dependency wiring in WORKSPACE)

---

## Phase 8: Cross-Platform Compilation (codec parity)

**Purpose**: Align `stream/` build config with `codec/` so it cross-compiles for host (macOS ARM64 + Linux x86_64) and Android arm64. Mirrors codec's platform aliases, `platforms/`, NDK registration, source-built deps, and mk/verify modules.

- [x] T054 Create `stream/platforms/platforms.bzl` + `stream/platforms/BUILD` defining `android_arm64_platform`, `linux_x86_64_platform`, `darwin_arm64_platform` (config_setting + platform, mirroring codec/platforms/)
- [x] T055 Update `stream/.bazelrc`: add `build:android_arm64 / linux_x86_64 / darwin_arm64` platform aliases, NDK toolchain registration (`--extra_toolchains=@androidndk//:all` + `--incompatible_enable_cc_toolchain_resolution=true`), and `build:shared` config
- [x] T056 Update `stream/WORKSPACE`: register `rules_android_ndk` (v0.1.2) + `android_ndk_repository(name="androidndk")` (lazy), remove the macOS Homebrew OpenSSL `new_local_repository` path
- [x] T057 Add OpenSSL source build to `stream/video_stream_deps.bzl` (http_archive + `//third_party/openssl:BUILD.bazel` build_file)
- [x] T058 Rewrite `stream/third_party/openssl/BUILD.bazel` to `configure_make` source build (static libssl.a + libcrypto.a, `--libdir=lib no-tests no-docs`)
- [x] T059 Fix `stream/third_party/libdatachannel/BUILD.bazel` `out_shared_libs` `.dylib` hardcode via `select()` (darwin `.dylib` / linux `.so`)
- [x] T060 Fix `stream/src/examples/BUILD.bazel` `@loader_path` rpath via `select()` (darwin `@loader_path` / linux+android `$ORIGIN`)
- [x] T061 Create `stream/scripts/verify/host_build.sh`, `host_verify.sh`, `android_build.sh` mirroring codec's scripts
- [x] T062 Add `stream/mk/host.mk`, `stream/mk/android.mk`, `stream/mk/clean.mk`, `stream/mk/aliases.mk` (host-build/verify, android-build, clean-out, aliases)
- [x] T063 Clean stale references in `stream/mk/test.mk` and `.github/workflows/stream-ci.yml` (`//tests/...`, `//src/test_server:whip_test_server` no longer exist)
- [x] T064 Add Android arm64 cross-build job to `.github/workflows/stream-ci.yml` (mirror codec `android.yml`: setup-android + `--config android_arm64`)
- [x] T065 Validate: host `bazel build //...` + `make host-verify`; Android `bazel build //src/core:stream_core //src/backend/mock:mock_backend --config android_arm64`

**Note**: OpenSSL build needed two follow-on fixes beyond the plan — `lib_source` in `third_party/*/BUILD.bazel` must reference the external repo explicitly (`@openssl//:all` etc.) because a relative `:all` resolves to the local `//third_party/<name>` package; and a `ar_wrapper.sh` shim routes Apple `libtool` (Bazel's macOS AR) to a `-o`-style invocation. Full webrtc backend Android cross-compile (openssl/libdatachannel/curl on NDK) remains a follow-up.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — can start immediately
- **Foundational (Phase 2)**: Depends on Setup — BLOCKS all user stories
- **US1 (Phase 3)**: Depends on Foundational — no dependency on other stories
- **US2 (Phase 4)**: Depends on Foundational — integrates with US1 backend registry
- **US4 (Phase 5)**: Depends on Foundational — can run in parallel with US1/US2
- **US3 (Phase 6)**: Depends on US1 (stream_impl) — ABR/reconnect integrate into stream_impl
- **Polish (Phase 7)**: Depends on all desired user stories

### User Story Dependencies

- **US1 (P1)**: Can start after Foundational — core push flow, no dependencies on other stories
- **US2 (P2)**: Can start after Foundational — backend registration and lifecycle, independent of US1 but may reference its types
- **US4 (P2)**: Can start after Foundational — standalone test server, independent of US1/US2/US3
- **US3 (P3)**: Depends on US1 (stream_impl) — ABR, reconnect, config tuning modify stream_impl

### Parallel Opportunities

- All Phase 1 Setup tasks marked [P] can run in parallel
- T013-T014 (mock backend) parallel with T019 (WebRTC dep) in Phase 3
- US2 (Phase 4) and US4 (Phase 5) can run in parallel
- All [P] tasks within a phase can run in parallel
- US3 (Phase 6) sequential — ABR, reconnect, backpressure all modify stream_impl

---

## Parallel Example: User Story 1

```bash
# Launch mock backend and WebRTC dependency in parallel:
Task: "Create mock_backend.h/.cc in stream/src/backend/mock/"
Task: "Add WebRTC dependency to video_stream_deps.bzl"

# Then sequential core implementation:
Task: "Create stream_impl.h/.cc in stream/src/core/"
Task: "Create webrtc_backend.h/.cc in stream/src/backend/webrtc/"
```

## Implementation Strategy

### MVP First (US1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: US1 (Push Media Stream)
4. **STOP and VALIDATE**: Push a stream, verify it works
5. MVP achieved — core streaming capability delivered

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add US1 (Push Media Stream) → **MVP: core push flow works**
3. Add US4 (Test Server) → End-to-end verification possible
4. Add US2 (Backend Selection) → Plugin architecture proven
5. Add US3 (Configuration) → ABR, reconnect, tuning complete

### Parallel Team Strategy

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: US1 (core push + WebRTC backend)
   - Developer B: US4 (test server)
3. After US1 completes:
   - Developer A: US2 (backend selection, lifecycle)
   - Developer B: US3 (ABR, reconnect, config tuning)