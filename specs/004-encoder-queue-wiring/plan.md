# Implementation Plan: Encoder-to-Queue Push Wiring

**Branch**: `004-encoder-queue-wiring` | **Date**: 2026-08-12 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/004-encoder-queue-wiring/spec.md`

## Summary

Wire the encoder to the output queue (spec-002 task T020). The encoder gains an optional
`OutputSink` attachment (`SetOutputSink`); when a sink is attached, every packet produced by
a successful `Encode()`/`Flush()` is handed to the sink (push mode, single destination), while
the pull API stays the default and unchanged when no sink is attached. The FFmpeg video/audio
backends implement the push path and the backend package gains a dependency on `queue`.
This makes the designed "encoder → queue → consumer → file" transport runnable with a real
encoder.

## Technical Context

**Language/Version**: C++17 (per spec `001-project-scaffold`)

**Build System**: Bazel 6.5.0

**Primary Dependencies**: Existing `core`, `api`, `utils`, `queue` (all in-repo); FFmpeg
backend (`@ffmpeg//:ffmpeg_codec*`) for the real-encoder integration test; googletest.

**Storage**: N/A (library project, no persistent storage)

**Testing**: googletest — new push-mode tests in `codec/tests/backend/ffmpeg/` (real
encoder → queue, order/loss under `kBlock`, pull unchanged, flush + EOS) and an api-level
test that `SetOutputSink` is rejected when unsupported. Existing tests must keep passing.

**Target Platform**: macOS ARM64 (development), Linux x86_64 (CI/release), Android arm64
(cross-build, MediaCodec backend unaffected)

**Project Type**: C++ static/shared library with a public C++ API

**Performance Goals**: Push path performs one move into the sink; no extra copies or
allocations beyond the existing `Encode()` path; blocking back-pressure naturally paces the
producer (no busy-wait, no packet loss).

**Constraints**:
- `api` MUST NOT depend on `queue` — `OutputSink` is forward-declared in the api headers;
  only `backend/*` (and consumers) include `queue/queue_iface.h`.
- Push mode is opt-in; pull API remains the default and unchanged.
- Encoders are not internally thread-safe (one encoder per thread); no new threads.
- When a sink is attached, a produced packet goes to exactly ONE destination (the sink);
  `Encode()` returns a moved-from/empty packet (FR-006).
- End-of-stream is caller-owned (`PacketSource::MarkEos`), since video+audio producers
  may share one queue — refinement of FR-005 (encoder flushes the sink; caller marks EOS).

**Scale/Scope**: Two api headers gain one method each; two FFmpeg backend encoders gain push
in `Drain()`/`Flush()`; backend/ffmpeg BUILD gains a `queue` dep; new tests. No changes to
`queue`, `consumer`, `public`, or the lifecycle state machine.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Constitution file (`.specify/memory/constitution.md`) is the placeholder template only — no
project-specific principles, constraints, or gates defined.

- **Gate 1 — Project principles**: No binding principles defined. PASS.
- **Gate 2 — Constraints**: No binding constraints defined. PASS.
- **Gate 3 — Governance**: No governance rules defined. PASS.

**Verdict**: All gates pass. Post-design re-check: the module-dependency rule
(`api` stays `queue`-free; `backend/ffmpeg` → `queue` is acyclic) and the frozen
pull-API contract are both honored.

## Project Structure

### Documentation (this feature)

```text
specs/004-encoder-queue-wiring/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0: push/EOS/ownership decisions
├── data-model.md        # Phase 1: sink attachment model
├── quickstart.md        # Phase 1: how to use push mode
├── contracts/
│   └── push-mode-contract.md   # Phase 1: SetOutputSink + push semantics contract
├── checklists/
│   └── requirements.md  # Spec quality checklist (all PASS)
└── tasks.md             # Phase 2 output (/speckit.tasks - NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
codec/src/framework/api/
├── video_encoder.h      # + virtual StatusCode SetOutputSink(OutputSink*) (fwd-declared)
└── audio_encoder.h      # + virtual StatusCode SetOutputSink(OutputSink*) (fwd-declared)

codec/src/framework/backend/ffmpeg/
├── video_encoder.h/.cc  # override SetOutputSink; push in Drain()/Flush()
├── audio_encoder.h/.cc  # override SetOutputSink; push in Drain()/Flush()
└── BUILD.bazel          # + dep "//src/framework/queue"

codec/tests/backend/ffmpeg/            # NEW package
├── BUILD.bazel                        # encode_push_test (+ deps on backend/ffmpeg, queue, gtest)
└── encode_push_test.cc                # real encoder -> queue -> drain; order/loss/EOS
```

**Structure Decision**: The `SetOutputSink` hook lives on the abstract encoders in `api`
(forward-declared `OutputSink`, default `kUnsupportedOperation`) so the contract is visible
on the public surface while `api` never depends on `queue`. The FFmpeg backends implement it;
they already hold the produced packet in `Drain()`, so push is a small, localized addition.
A new `tests/backend/ffmpeg` package hosts the real-encoder integration test (FFmpeg is
cached in this workspace, so the test links quickly).

## Complexity Tracking

N/A — No constitution violations. The `backend/ffmpeg → queue` dependency is acyclic and
consistent with the design's hand-off role (`plan.md` mermaid in spec 002 shows backends
handing `Packet` to the queue). Caller-owned EOS is a documented refinement, not a
violation.
