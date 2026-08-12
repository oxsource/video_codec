# Research: Architecture & Engineering Design (Phase 0)

Resolves the open questions from `spec.md` §5 and feeds the design in `plan.md` /
`data-model.md` / `contracts/`. Each item records **Decision**, **Rationale**, and
**Alternatives considered**.

---

## R1. Hermetic x264 dependency

**Decision**: Add `libx264` as its own `rules_foreign_cc` `configure_make` target
(`@x264`) and point FFmpeg's `--extra-cflags` / `--extra-ldflags` at its outputs; drop
the dev-host Homebrew dependency.

**Rationale**:
- The current `third_party/ffmpeg/BUILD.bazel` links x264 from `/opt/homebrew/lib`
  (via `PKG_CONFIG_PATH` + explicit flags), which makes the build non-hermetic and
  breaks on Linux CI and on hosts without Homebrew x264.
- `rules_foreign_cc` already drives FFmpeg; adding x264 the same way keeps one build
  mechanism and a fully reproducible, offline-capable build.

**Alternatives considered**:
- *Keep Homebrew x264*: rejected — non-reproducible across machines/CI, contradicts
  the hermetic-Bazel goal.
- *Vendor x264 sources in-tree*: rejected — larger repo, manual sync; `http_archive`
  + `configure_make` is the standard pattern.

---

## R2. Android NDK wiring

**Decision**: Register `android_ndk_repository(name = "androidndk")` in `WORKSPACE`
(constitution/init step) and rewire `//third_party/android_ndk` from a header-only
wrapper to expose `@androidndk//:media` (the real `NdkMediaCodec.h` toolchain lib).
Keep `target_compatible_with = ["@platforms//os:android"]` on `mediacodec_spike` so the
host build stays NDK-free.

**Rationale**:
- `mediacodec_spike` currently references `@androidndk//:media` which is not registered,
  so the Android cross-build cannot link (tasks.md T022/T023). Registering the repo is
  the only missing piece.
- `select()`-per-platform already excludes the NDK from non-Android builds.

**Alternatives considered**:
- *Hand-rolled `cc_library` over NDK headers*: rejected — loses the toolchain's
  prebuilt `libmediandk`, version drift, and proper target triple handling.

---

## R3. VideoToolbox backend scope

**Decision**: Keep `backend/darwin` as a **reserved** location only. On Apple platforms
the `Create` factory falls back to the FFmpeg backend (as in Phase 1). Defer the full
VideoToolbox design + implementation to a later phase. Document the fallback rule in
`codec/doc/architecture/backend-selection.md` and ADR-004.

**Rationale**:
- `project_bootstrap.md` already scopes VideoToolbox as Phase 2+. The scaffold spike and
  FFmpeg backend are the validated MVP path; VideoToolbox is a performance optimization,
  not a correctness blocker.
- Designing it now would duplicate the `backend/ffmpeg` contract work with no runtime to
  test against.

**Alternatives considered**:
- *Design VideoToolbox fully now*: rejected — no encoder implementation to contract
  against yet; premature.

---

## R4. Testing framework & structure

**Decision**: Use **googletest 1.14.0** (already pinned in `video_codec_deps.bzl`). Three
tiers:
- **unit**: `core` / `utils` pure-logic tests (frame/packet construction, format
  conversion round-trips).
- **integration**: `backend/ffmpeg` and `backend/android` encode a few frames and assert
  the output decodes (reuse `ffprobe` / Android decoder) — gated behind the backend's
  platform.
- **smoke**: the existing `*_spike` targets double as smoke tests (no `bazel test`
  target required, but they validate end-to-end).

**Rationale**: googletest is already a dependency; the scaffold `tests/BUILD.bazel`
placeholder exists. Reusing spikes as smoke tests avoids duplicating encode logic.

**Alternatives considered**:
- *Catch2*: rejected — adds a second test framework; googletest already present.
- *No test framework (asserts only)*: rejected — no structured test reporting.

---

## R5. CI platform matrix

**Decision**: GitHub Actions with a 3-job matrix:
- `macos-arm64`: `bazel build //...` + `bazel test //...` + clang-format + clang-tidy.
- `linux-x86_64`: same as above (the release/build reference).
- `android`: `bazel build //src/spike:mediacodec_spike --platforms=//platforms:android_arm64_platform`
  (requires R2's NDK registration).

**Rationale**: matches the dev (macOS ARM64) and CI/release (Linux x86_64) targets from
`project_bootstrap.md`; Android cross-build is the only NDK path and is cheap to gate.

**Alternatives considered**:
- *Self-hosted macOS + Linux runners only, skip Android*: rejected — Android is a
  primary backend; a cross-build gate is low cost and catches NDK breakage early.
- *Bazel's own CI / Buildkite*: rejected — GitHub Actions is the native_ui reference
  and the repo has no other CI.

---

## R6. Logging strategy

**Decision**: Define a `LogSlot` abstract interface (virtual `Log(Level, const char*)`,
default no-op) in `core`. Framework code logs through a process-wide slot pointer the
consumer sets once. No logging library is a hard dependency.

**Rationale**:
- Keeps the library dependency-free for consumers (mirrors native_ui's `LogSlot`).
- Lets Android use `__android_log_print`, desktop use `spdlog`/fprintf, tests use a
  capture slot — without the framework caring.

**Alternatives considered**:
- *Link spdlog directly*: rejected — imposes a logging choice on every consumer.
- *printf everywhere*: rejected — no level filtering, no pluggability.

---

## R7. Error-handling model

**Decision**: A `StatusCode` enum (OK + per-category errors: invalid-argument,
not-initialized, encode-failed, unsupported-format, backend-unavailable,
platform-unsupported) returned by all fallible APIs; a `Result<T>` wrapper
(`Result<T> = StatusCode | T`) for value-returning fallible calls. **No exceptions cross
the public API.** `VIDEO_CODEC_API` surfaces only `StatusCode` / `Result<T>`.

**Rationale**: C ABI-friendly (the library may be consumed from other languages),
predictable, matches the "library-first" principle. Exceptions would complicate the C
boundary and the Android JNI/NDK interop.

**Alternatives considered**:
- *C++ exceptions*: rejected — cross-API-boundary exceptions are unsafe and hurt the C
  consumers; also disabled by many embedded/NDK toolchains by default.
- *`std::optional` + out-param*: rejected — loses the error *reason*.

---

## R8. FFmpeg link model (confirmed, not new)

**Decision (carried from Phase 1, recorded as ADR-001/003)**: FFmpeg is linked as a
single **static, force-loaded** archive. `configure_make` builds `libavcodec.a` +
`libavutil.a`; a `postfix_script` merges them with `/usr/bin/libtool -static` into one
BSD-format `libffmpeg.a` (avoids `ar -x` clobbering the duplicate-basename
`videodsp.o` and the GNU-format `ld64` rejection). Consumers `-force_load` it so the
aarch64 NEON helper defining `_ff_prefetch_aarch64` is never lazily dropped.

**Rationale**: a shared `.dylib` bakes the long sandbox prefix into `LC_ID_DYLIB` and
overflows `cmdsize`; lazy static linking drops unreferenced internal members → dyld
"symbol not found". Force-load + merge solves both.

**Alternatives considered**: see `codec/doc/adrs/ADR-001-ffmpeg-static-forceload.md` and
`ADR-003-bsd-libtool-merge.md`.

---

## R9. Encoded-output transport (ring buffer)

**Decision**: Hand off encoded packets to consumers through a **bounded SPSC ring
buffer** (`EncodedPacketQueue`), exposed as two interfaces — `OutputSink` (producer /
encoder writes via `Submit`) and `EncodedPacketSource` (consumer pops via `TryPop`/`Pop`).
Implementation is lock-free via atomic `head_`/`tail_` indices over a fixed power-of-two
slot array; packets are **moved** in and out (no per-packet allocation on the hot path).
Back-pressure when full is configurable: `kBlock`, `kDropOldest`, or `kError` (default —
fail loud so a misconfigured/slow consumer surfaces instead of silently losing or hanging).

**Rationale**:
- The encoder runs synchronously on its own thread (see `threading.md`); the consumer
  (muxer / network / file) runs on another. A ring buffer is the standard
  producer→consumer hand-off that decouples their rates without copying through the
  caller on every frame.
- SPSC lock-free is the simplest correct shape (one writer, one reader); it avoids mutex
  contention on the encode hot path.
- Move semantics keep allocation off the critical path; fixed slots avoid runtime
  `new`/`delete` per packet.
- Configurable back-pressure makes the same queue usable for live streaming (`kBlock`),
  lossy real-time (`kDropOldest`), and strict pipelines (`kError`).

**Alternatives considered**:
- *Return packet to caller, let app manage a queue*: shifts the (non-trivial) MPMC/SPSC
  decision onto every consumer; no shared, tested component.
- *Unbounded queue*: risks unbounded memory under a slow consumer (live capture).
- *Mutex + `std::queue`*: correct but adds lock contention on every encode; SPSC
  lock-free is strictly better for the common single-consumer case.
- *MPMC queue*: more general than needed; harder to make wait-free and to reason about
  for the single-producer/single-consumer encode→mux path.

## Open clarifications — resolved

All `NEEDS CLARIFICATION` items from the Technical Context are resolved above; none
remain. The design in `plan.md` and the contracts are unblocked.
