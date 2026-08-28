# video_codec Architecture

Index of the framework's engineering design. This phase (spec `002-architecture-engineering-design`)
produces **design artifacts only** — the implementation phase follows these contracts.

## Modules

`core` · `api` · `utils` · `backend/{android,ffmpeg,darwin}` · `public` · `queue`

Dependency rule: arrows point inward; backends never depend on each other; only `public`
is externally visible. See [module-dependencies.md](module-dependencies.md).

## Key models

- [lifecycle-model.md](lifecycle-model.md) — encoder lifecycle state machine
- [backend-selection.md](backend-selection.md) — factory + `select()` + `backend`
- [threading.md](threading.md) — sync-by-default, caller-owns-thread
- [error-handling.md](error-handling.md) — `Status` / `Result<T>`, no exceptions
- [logging-slot.md](logging-slot.md) — pluggable `LogSlot`
- [output-queue.md](output-queue.md) — ring-buffer transport (encoder → consumer)

## Engineering standards (repo root `doc/`)

- [../build-conventions.md](../build-conventions.md)
- [../testing-strategy.md](../testing-strategy.md)
- [../ci-strategy.md](../ci-strategy.md)
- [../release-process.md](../release-process.md)

## Architecture Decision Records

`../adrs/`:

- [ADR-001](../../adrs/ADR-001-ffmpeg-static-forceload.md) — FFmpeg static, force-loaded
- [ADR-002](../../adrs/ADR-002-select-per-platform-backend.md) — `select()` per platform
- [ADR-003](../../adrs/ADR-003-bsd-libtool-merge.md) — BSD `libtool` merge
- [ADR-004](../../adrs/ADR-004-deferred-videotoolbox.md) — deferred VideoToolbox
- [ADR-005](../../adrs/ADR-005-ringbuffer-transport.md) — SPSC ring-buffer output transport
