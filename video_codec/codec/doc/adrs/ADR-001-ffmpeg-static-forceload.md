# ADR-001: FFmpeg linked as a static, force-loaded archive

- **Status**: Accepted
- **Date**: 2026-08-11

## Context

The scaffold must produce a runnable binary that encodes H.264 via FFmpeg `libx264` on
macOS ARM64 and Linux x86_64. A naive `http_archive` + source `glob` fails (no
`config.h`); a shared `.dylib` fails (sandbox prefix overflows `LC_ID_DYLIB` `cmdsize`);
and a plain static archive fails at runtime with `dyld: symbol not found in flat
namespace '_ff_prefetch_aarch64'` because unreferenced internal members (the aarch64
NEON helper) are lazily dropped.

## Decision

Build FFmpeg from source via `rules_foreign_cc` `configure_make`. Merge
`libavcodec.a` + `libavutil.a` into one BSD-format `libffmpeg.a`, expose it through a
`genrule` → `cc_import` → `alwayslink` `cc_library`, and have consumers
**`-force_load`** the archive so no member is lazily dropped.

## Consequences

- The `_ff_prefetch_aarch64` NEON symbol (and all internal helpers) is always present at
  load → no dyld failure.
- No shared-library Mach-O corruption.
- Slightly larger binaries (whole archive linked), acceptable for an encoder library.

## Alternatives rejected

- *Shared `.dylib`*: bakes the long sandbox prefix into `LC_ID_DYLIB`, overflowing
  `cmdsize` and corrupting the Mach-O.
- *Plain static (lazy)*: drops unreferenced internal members → runtime symbol-not-found.
- *`--all_load` linkopt*: inert because Bazel emits target/dep linkopts *after* the
  archive on the link line; `-force_load` on an explicit single-output path is required.
