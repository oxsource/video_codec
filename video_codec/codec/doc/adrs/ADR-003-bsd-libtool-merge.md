# ADR-003: Merge FFmpeg archives with BSD `libtool -static`

- **Status**: Accepted
- **Date**: 2026-08-11

## Context

`rules_foreign_cc` `configure_make` emits `libavcodec.a` + `libavutil.a`. We need one
archive the consumer can `-force_load`. Two pitfalls: (a) `ar -x` extraction flattens to
basenames and **silently clobbers** `libavcodec`'s two `videodsp.o` members (one C, one
aarch64 asm that *defines* `_ff_prefetch_aarch64`), dropping that definition; (b) the
installed archives are GNU-format on this host (Homebrew `ar` wins on `PATH`), which
macOS `ld64` rejects with "archive member invalid control bits".

## Decision

In `configure_make`'s `postfix_script`, merge **without extracting** using
`/usr/bin/libtool -static -o libffmpeg.a libavcodec.a libavutil.a`, then `ranlib` and
remove the inputs. `libtool` preserves every member (including duplicate basenames) and
writes BSD-format output `ld64` accepts.

## Consequences

- No member clobbering → the aarch64 NEON helper definition survives.
- BSD-format archive links cleanly on macOS.
- Ties directly to ADR-001 (force-load the merged archive).

## Alternatives rejected

- *`ar -x` then re-archive*: clobbers duplicate-basename `videodsp.o` → undefined symbol.
- *Keep two separate archives, force-load both*: GNU-format members still rejected by
  `ld64`; and two `-force_load` inputs complicate the link line.
