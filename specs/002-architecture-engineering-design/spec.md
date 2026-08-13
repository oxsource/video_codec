# Feature Specification: Architecture & Engineering Design

**Spec**: 002-architecture-engineering-design | **Date**: 2026-08-12 | **Phase**: Design (pre-implementation)

**Input**: Design vision from `codec/doc/project_bootstrap.md` (Phase 1); validated
scaffold from spec `001-project-scaffold` (FFmpeg + MediaCodec spikes, Bazel layout,
`make`-based validation). This spec does **not** add runtime code — it is the
authoritative engineering design that the implementation phase will follow.

---

## 1. Purpose

Phase 1 (`001-project-scaffold`) proved the two highest-risk dependencies work on the
dev host (FFmpeg `libavcodec` produces a valid H.264 stream via `libx264`; Android
`MediaCodec` source compiles under the Android toolchain) and established the Bazel
workspace, platform `select()`, and a categorized `make` validation mechanism.

Phase 2 (this spec) turns the design vision (`project_bootstrap.md`) into a concrete,
reviewable engineering design: module boundaries and visibility, interface contracts,
encoder lifecycle, backend-selection model, threading, error handling, logging, and
the build / test / CI / release standards. Output is **design artifacts only**
(architecture docs, API contracts, conventions, CI configs) — no `src/framework/*.cc`
implementation yet.

## 2. Goals

- **Module architecture**: define `core` / `api` / `backend/*` / `utils` / `public`
  boundaries, dependency direction, and Bazel `visibility` rules.
- **Interface contracts**: pin the public surface (`VideoEncoder` / `AudioEncoder` /
  `InputSurface` / `NativeBuffer`) and the backend contract each platform implements.
- **Lifecycle & selection**: formalize the encoder lifecycle state machine and the
  factory/backend-selection rule (compile-time platform macro + `backend`).
- **Error handling**: a uniform `Result<T>` / status-code strategy across all modules.
- **Ownership model**: `std::unique_ptr` for owned objects, raw non-owning pointers,
  and the `NativeBuffer` pointer-object convention for zero-copy handles.
- **Threading model**: define on which thread encode runs, async vs sync, and the
  contract for re-entrancy / thread-safety.
- **Logging slot**: a pluggable `LogSlot` interface so consumers inject their own
  logger (no hard dependency on a logging library).
- **Engineering standards**: BUILD-file conventions, testing strategy (unit +
  integration + smoke), CI matrix (macOS ARM64 / Linux x86_64 / Android cross-build),
  release & versioning process.
- **ADRs**: record the load-bearing decisions (static force-loaded FFmpeg archive,
  `select()`-per-platform backends, BSD `libtool` merge, deferred VideoToolbox).

## 3. Non-Goals (this design phase)

- **No runtime encoder code**: `src/framework/backend/*` stays stub until the
  implementation phase. This spec designs *how* they will be built.
- **No decoder, muxer, filters, network** (deferred per `project_bootstrap.md`).
- **VideoToolbox backend not designed in detail** — only its reserved location and the
  fallback rule (Apple → FFmpeg) are specified; full design deferred to a later phase.

## 4. Requirements

### FR-1 Module boundaries
Every backend is an independent subdirectory under `src/framework/backend/` with its
own `BUILD.bazel` and dependency closure; backends never depend on each other. Business
code depends only on `api/`, wired to a backend via `select()` at the `public`/consumer
layer.

### FR-2 Public API contract
`VideoEncoder` / `AudioEncoder` expose `Create()` (factory), `Init()`, `Encode()`
(CPU frame and `NativeBuffer` overloads), `CreateInputSurface()`, `Flush()`,
`Release()`. Signatures are frozen by `contracts/public-api.md` and `contracts/encoder-contract.md`.

### FR-3 Backend contract
Each backend implements the `VideoEncoder` / `AudioEncoder` abstract interface and the
`NativeBuffer`/`InputSurface` semantics for its platform. Frozen by
`contracts/backend-contract.md`.

### FR-4 Lifecycle
Encoders follow a single lifecycle: `Created → Initialized → Encoding → Flushed →
Released`. Invalid transitions (e.g. `Encode` before `Init`) return an error, never
crash. Modeled as a state diagram in `doc/architecture/lifecycle-model.md`.

### FR-5 Error handling
All fallible operations return a `Status` (or `Result<T>`); no exceptions cross the
public API. Strategy in `doc/architecture/error-handling.md`.

### FR-6 Logging slot
Framework logging goes through a `LogSlot` abstract interface; default no-op, consumer
plugs a concrete impl. Design in `doc/architecture/logging-slot.md`.

### FR-7 Engineering standards
BUILD conventions (`doc/build-conventions.md`), testing strategy
(`doc/testing-strategy.md`), CI matrix (`doc/ci-strategy.md`), release process
(`doc/release-process.md`).

## 5. Open Questions resolved by research.md

- Hermetic x264: add x264 as its own `rules_foreign_cc` target vs keep dev-host Homebrew.
- Android NDK: register `android_ndk_repository(name = "androidndk")` and rewire the
  `//third_party/android_ndk` wrapper to `@androidndk//:media`.
- Testing: googletest (already pinned) for unit + integration; smoke tests reuse spikes.
- CI: GitHub Actions matrix (macOS ARM64, Linux x86_64, Android cross-build).

## 6. Success Criteria

- All architecture docs and contracts exist under `codec/doc/architecture/` and
  `specs/002-.../contracts/` and are internally consistent.
- A reviewer can implement `backend/ffmpeg` and `backend/android` from the contracts
  alone, without further design decisions.
- CI config builds + tests on the target matrix and cross-builds Android.
- ADRs document every load-bearing choice with rationale and rejected alternatives.
