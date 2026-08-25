# Research: Stream Module Architecture

## Decision 1: Stream Module Location

- **Decision**: Stream module as a top-level directory `stream/` parallel to `codec/`, within the same repository but as a separate Bazel workspace. Bazel, Makefile, and CI are rebuilt independently to enable future repo split.
- **Rationale**:
  - "平行层级" (parallel hierarchy) — stream is a peer to codec, not a submodule
  - Separate workspace avoids Bazel `select()` complexity and keeps build graphs independent
  - Stream depends on codec's public API (`video_codec`), which is already a stable published interface
  - Independent `Makefile` and CI targets follow the existing pattern
  - Can be built and tested independently (SC-005)
  - Enables future repository split without restructuring
- **Alternatives Considered**:
  - Within `codec/src/stream/`: Tighter coupling, stream would be part of the codec workspace; contradicts "平行独立" (parallel independent)
  - Separate repository: Too much overhead for v1; CI and cross-repo versioning add complexity

## Decision 2: WebRTC Dependency Strategy

- **Decision**: Use a C++ WebRTC library (e.g., `libwebrtc` via `rules_foreign_cc` or system package) with a thin abstraction layer so the backend can be swapped
- **Rationale**:
  - Matching existing dependency pattern (FFmpeg built from source via `rules_foreign_cc`)
  - WHIP protocol is a lightweight HTTP-based signaling layer on top of WebRTC ICE/DTLS/SRTP
  - The abstraction layer (`StreamBackend` interface) already insulates the rest of the stream module from WebRTC specifics
- **Alternatives Considered**:
  - Pure WebRTC without abstraction: Violates FR-003 (backend plugin architecture)
  - Custom UDP/RTP implementation: Unnecessary, reinvents what WebRTC already provides

## Decision 3: Network Resilience Strategy

- **Decision**: ICE restart for cellular handover, exponential-backoff reconnection for total signal loss, ring-buffer for local caching during disconnection
- **Rationale**:
  - WebRTC ICE restart handles IP address changes (cell tower handover) natively
  - Exponential backoff (1s, 2s, 4s, 8s, max 30s) prevents server overload during fleet-wide reconnection
  - Ring buffer (configurable duration, default 30s) matches vehicle tunnel scenario timing
  - ABR controller monitors packet loss and RTT via WebRTC stats to adjust bitrate in real-time

## Decision 4: Test Server Architecture

- **Decision**: Standalone binary using the same WebRTC stack as the client, implementing WHIP server endpoint and serving a browser player page via embedded HTTP
- **Rationale**:
  - Reuses the same WebRTC library, simplifying dependency management
  - WHIP server endpoint is well-defined (POST to create, PATCH for ICE, DELETE to teardown)
  - Browser player page uses standard WebRTC APIs (RTCPeerConnection) to subscribe — no additional streaming protocol needed
  - Embedded HTTP server (e.g., C++ REST library or minimal socket implementation) keeps it lightweight