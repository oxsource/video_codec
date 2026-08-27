# Implementation Plan: Stream Interface

> **Status: historical plan.** The implementation has since evolved; read
> [webrtc-backend-context.md](webrtc-backend-context.md), [quickstart.md](quickstart.md),
> [data-model.md](data-model.md) and `stream/README.md` for the current state.
> Notable drifts from this plan: transport uses **libdatachannel** (not
> libwebrtc), the in-repo WHIP `test_server/` was removed (MediaMTX is the
> verification endpoint), and OpenSSL/libcurl/JSON config moved as described in
> the docs above.

**Branch**: `008-stream-interface` | **Date**: 2026-08-24 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/008-stream-interface/spec.md`

## Summary

Add a stream module as a peer to the existing codec layer, providing a unified interface for pushing encoded media streams with pluggable transport backends. v1 ships with a WebRTC/WHIP backend. Primary use case: vehicle video monitoring over 4G/5G mobile networks, requiring adaptive bitrate, auto-reconnect with local buffering, and network resilience. Includes a lightweight WHIP test server as a standalone binary for development and CI validation.

## Technical Context

**Language/Version**: C++17 (matching existing codec convention)

**Primary Dependencies**: libwebrtc (or WebRTC-agnostic abstraction), existing `codec` module for consuming encoded media; WHIP protocol support

**Storage**: Local file buffer for disconnected-period caching (ring buffer or temp file)

**Testing**: Google Test (matching existing codec convention); mock transport backend for unit tests; WHIP test server for integration tests

**Target Platform**: Linux ARM64 (vehicle device), Linux x86_64, macOS ARM64 (development)

**Project Type**: C++ library (parallel module to codec)

**Performance Goals**: 720p30 over 4G (2 Mbps variable), <3s startup, <1% frame drop under stable conditions, <3% under mobile network simulation

**Constraints**: Vehicle-mounted device with 4G/5G mobile network; frequent signal loss, variable bandwidth, cellular handover; must not depend on any GUI framework

**Scale/Scope**: Single session per instance; multiple instances for multi-camera vehicles

**Unknowns**: Resolved by research.md. Stream module is a top-level directory `stream/` parallel to `codec/`, as a separate Bazel workspace within the same repository. Bazel, Makefile, and CI are rebuilt independently to enable future repo split.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Constitution file (`.specify/memory/constitution.md`) is a template with no concrete principles defined. No gates to enforce at this time.

## Project Structure

### Documentation (this feature)

```text
specs/008-stream-interface/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
stream/                           # Independent workspace, parallel to codec/
├── BUILD.bazel                   # Root alias target
├── WORKSPACE                     # workspace(name = "video_stream")
├── Makefile                      # Module system (following codec/mk/ pattern)
├── video_stream_deps.bzl         # Dependency bootstrap (libwebrtc, etc.)
├── .bazelrc                      # C++17, platform aliases
├── .bazelversion                 # 6.5.0 (matching codec)
├── src/
│   ├── api/
│   │   ├── BUILD.bazel
│   │   ├── stream.h              # Unified stream interface
│   │   ├── stream_backend.h      # Abstract backend contract
│   │   ├── stream_config.h       # Stream configuration
│   │   └── stream_status.h       # Status/observability
│   ├── backend/
│   │   ├── webrtc/
│   │   │   ├── BUILD.bazel
│   │   │   ├── webrtc_backend.cc / .h
│   │   │   └── whip_session.cc / .h
│   │   └── mock/
│   │       ├── BUILD.bazel
│   │       └── mock_backend.cc / .h
│   ├── core/
│   │   ├── BUILD.bazel
│   │   ├── stream_impl.cc / .h    # Default stream implementation
│   │   ├── abr_controller.cc / .h # Adaptive bitrate logic
│   │   └── reconnect_handler.cc / .h
│   └── test_server/
│       ├── BUILD.bazel
│       ├── whip_test_server.cc / .h
│       └── web/
│           └── player.html
├── tests/
│   ├── BUILD.bazel
│   ├── stream_interface_test.cc
│   ├── abr_controller_test.cc
│   ├── reconnect_handler_test.cc
│   └── webrtc_backend_test.cc
└── mk/                           # Make module system (following codec pattern)
    ├── rules.mk
    ├── build.mk
    ├── test.mk
    └── help.mk
```

**Structure Decision**: `stream/` as an independent Bazel workspace parallel to `codec/`. Bazel, Makefile, and CI are rebuilt independently to enable future repo split. The stream module depends on the codec's public API (`video_codec`) as an external dependency consumed via Bazel's `local_repository` or path-based dep.

## Complexity Tracking

No constitution violations to justify at this time.