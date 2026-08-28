# Module Dependencies & Visibility

## Dependency graph

```
public ──▶ api ──▶ core
   ├──▶ utils ──▶ core
   ├──▶ queue ──▶ core
   ├──▶ io ──▶ core
   ├──▶ convert/libyuv ──▶ core, utils, (external dep: @libyuv)
   ├──▶ consumer ──▶ core, queue, io
   └──▶ backend/* ──▶ api, core, utils, io, (external dep)
```

- `core` depends on **nothing** (leaf; owns `PacketSink`, shared by the queue,
  consumers, and the api Muxer interface).
- `api` → `core` only (`Muxer` interface inherits `core::PacketSink`; `ByteSink`
  is forward-declared so api stays free of an `io` dependency).
- `utils` → `core` only.
- `queue` → `core` only (ring buffer over `VideoPacket`/`AudioPacket` on two
  independent rings).
- `io` → **nothing** (leaf; the single `ByteSink` contract + file/stream/tee
  sinks — a network output is just a non-seekable ByteSink).
- `convert/libyuv` → `core`, `utils`, `@libyuv` (neutral pixel conversion).
- `consumer` → `core`, `queue`, `io` (implements `core::PacketSink`: raw
  Annex-B / ADTS file sink).
- Each `backend/<x>` → `api`, `core`, `utils`, `io`, **and exactly one** external
  dependency (`@ffmpeg` / `@androidndk` / `VideoToolbox.framework`).
- `backend/ffmpeg` additionally implements the api `Muxer` interface (MP4 via
  libavformat) writing through `io::ByteSink` — muxing is a backend capability,
  not a standalone module.
- `backends` NEVER depend on each other.
- `public` → all of the above; it is the **only** externally-visible package.

## Visibility rules

| Package | `visibility` |
|---------|--------------|
| `core`, `api`, `utils`, `backend/*`, `queue`, `io`, `consumer` | `["//src/framework:__subpackages__", "//tests:__subpackages__"]` |
| `public` | `["//visibility:public"]` |
| `third_party/ffmpeg`, `third_party/android_ndk` | `["//visibility:public"]` (only `backend/*` may consume) |

## Invariants (CI-enforced)

1. No edge may point **out** of `core` (it is a leaf).
2. No edge between two `backend/*` packages.
3. Only `public` may be depended on by non-`src/framework` code.
4. A backend may only reference its own external dep; e.g. `backend/ffmpeg` must not
   reference `@androidndk`, and vice-versa.
5. `consumer` depends on `queue` (it drains the ring buffer) but `queue` does NOT depend
   back on `consumer` (the ring buffer is consumer-agnostic).

A Bazel visibility query in CI (`bazel query`) can assert (3); (1)/(2)/(5) are structural
and reviewed in PRs. See [build-conventions.md](../build-conventions.md).
