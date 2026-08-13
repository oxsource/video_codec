# ADR-002: Backends selected via `select()` at link time

- **Status**: Accepted
- **Date**: 2026-08-11

## Context

The framework supports multiple encoding backends (Android MediaCodec, FFmpeg, reserved
VideoToolbox). A non-Android build must not pull the NDK; a non-desktop build must not
pull FFmpeg unless chosen. Runtime `if` switching alone would still link every backend
and every external dependency into every binary.

## Decision

Each backend is an independent `BUILD.bazel` subdirectory. The `public` target links the
backend via `select({ "//platforms:<name>": ["//src/framework/backend/<x>"], ... })`, so
Bazel includes **only** the target platform's backend and its external dependency.
Runtime `Create()` resolves the concrete class from a platform macro (and an optional
`backend` override).

## Consequences

- Minimal binary footprint per platform (no dead backends / external deps linked).
- Adding a backend = new subdirectory + one `select()` entry; no central `#ifdef` maze.
- `backend` can override runtime choice but cannot pull a backend the binary did
  not link (it returns `nullptr` instead).

## Alternatives rejected

- *Runtime `if` with all backends linked*: bloats binaries, pulls NDK/FFmpeg everywhere.
- *Single backend, no abstraction*: kills cross-platform reuse and the "write once,
  encode anywhere" goal.
