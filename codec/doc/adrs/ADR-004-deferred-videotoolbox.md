# ADR-004: VideoToolbox backend deferred

- **Status**: Accepted
- **Date**: 2026-08-11

## Context

`project_bootstrap.md` scopes Apple `VideoToolbox` (HW encode) as Phase 2+. The scaffold
validates the FFmpeg path (the MVP). Designing/full-implementing VideoToolbox now would
duplicate the `backend/ffmpeg` contract work with no runtime to test against, and it is a
performance optimization, not a correctness blocker.

## Decision

`backend/darwin/` exists as a **reserved** location only. On Apple platforms the
`Create` factory falls back to the FFmpeg backend (`ResolveBackend` returns `kFFmpeg` for
`__APPLE__`). The full VideoToolbox design + implementation is deferred to a later phase;
this phase documents only the fallback rule and the reserved slot.

## Consequences

- Apple users get a working (software/FFmpeg) encoder immediately.
- No half-finished VideoToolbox code in the tree.
- Future VideoToolbox work slots into `backend/darwin/` behind the same `encoder-contract.md`.

## Alternatives rejected

- *Design + implement VideoToolbox now*: premature; no backend implementation to
  contract against; risks dead code if the API shifts after the FFmpeg backend lands.
