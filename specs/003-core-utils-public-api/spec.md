# Feature Specification: Core Utilities & Public API Surface

**Feature Branch**: `003-core-utils-public-api`

**Created**: 2026-08-12

**Status**: Draft

**Input**: User description: "按照项目需求及架构设计等，新建提案实现核心的类型、日志、及接口定义等" — redirected (per user) to the **not-yet-implemented** foundational pieces: the core utilities module and the public umbrella / API export surface.

## User Scenarios & Testing *(mandatory)*

This feature closes two remaining gaps in the library's **foundation layer** (the core
types, error model, logging slot, and abstract encoder interfaces were already implemented
in an earlier commit). It delivers (1) a media utilities module that backends and examples
use to prepare and interpret frames/packets, and (2) the single public include surface that
makes the library consumable by application code. Without these, downstream backends and the
end-to-end example cannot be completed.

### User Story 1 - Core media utilities (Priority: P1)

A backend implementer or example author needs standard, tested helpers to convert between
common pixel formats (e.g., planar YUV420P ↔ semi-planar NV12), compute row strides for a
given width/format, and convert between PCM sample formats — so each backend does not invent
its own, divergent, and possibly buggy conversion code.

**Why this priority**: The FFmpeg/AAC backend and the encode-to-file example both depend on
these helpers to feed the encoder and to validate output. It is on the critical path to a
working MVP and is foundational (depends only on the core types).

**Independent Test**: Can be verified by a unit test that, for each supported conversion,
round-trips a known buffer (convert forward then back, or compare against a hand-computed
reference) and asserts bit-exact or within-tolerance equality; stride computation is checked
against expected byte layouts for representative widths.

**Acceptance Scenarios**:

1. **Given** a YUV420P buffer, **When** it is converted to NV12 and back, **Then** the
   reconstructed buffer matches the original within tolerance (lossless for the supported
   path).
2. **Given** a width and pixel format, **When** the stride helper is called, **Then** it
   returns the correct per-row byte count used by the encoder backend.
3. **Given** an unsupported or invalid conversion request, **When** the helper is invoked,
   **Then** it reports a clear error status rather than producing a corrupt buffer.

---

### User Story 2 - Public umbrella header & API export (Priority: P1)

A library consumer wants to include a single header and obtain the stable public API
(encoders, types, error model, logging hook) without pulling in internal modules, and wants
the public symbols correctly exported so the library links whether built statically or as a
shared object.

**Why this priority**: This is what makes the library actually usable; it is the only
publicly visible surface. Nothing downstream (examples, consumers) can compile against the
library until this exists.

**Independent Test**: Can be verified by a compile test that includes only the umbrella
header and calls a public factory/encoder entry point, succeeding without referencing any
internal header; and by confirming the export macro resolves correctly for both static and
shared build configurations.

**Acceptance Scenarios**:

1. **Given** a consumer program that includes only the umbrella header, **When** it is
   compiled, **Then** it builds using exclusively public symbols and no internal headers.
2. **Given** a shared-library build, **When** the public API is used by an external caller,
   **Then** the symbols are exported and link successfully.
3. **Given** the umbrella header, **When** it is inspected, **Then** it re-exports exactly the
   intended public contracts (encoders, core types, error model, logging hook) and nothing
   internal.

---

### Edge Cases

- What happens when a requested pixel-format conversion is unsupported? → The helper returns
  an error status; the caller must handle it and must not assume a successful conversion.
- How are row strides handled for widths that are not multiples of the format's natural
  alignment? → The stride helper must return the padded layout the backend expects, and
  callers must respect it; mismatched stride is a caller error reported via the error model.
- What if the library is built as a shared object on a platform with different export
  semantics? → The export macro must select the correct visibility/declspec per platform and
  build mode, defaulting to no decoration for the static case.
- How is the public surface kept minimal? → Only the intended contracts are re-exported;
  adding an internal type to the public surface requires an explicit decision.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The library MUST provide a utilities module supporting conversion between at
  least planar YUV420P and semi-planar NV12 pixel formats.
- **FR-002**: The utilities module MUST provide a stride helper that computes the correct
  per-row byte count for a given width and pixel/sample format.
- **FR-003**: The utilities module MUST provide PCM sample-format conversion between the
  common layouts the audio encoder consumes.
- **FR-004**: All utilities MUST depend only on the core types and MUST NOT introduce a hard
  dependency on any specific encoder backend.
- **FR-005**: The utilities MUST report unsupported or invalid operations via the library's
  uniform error model (status code / result wrapper), never by producing corrupt output.
- **FR-006**: The library MUST ship a single public umbrella header that re-exports the
  intended public contracts (encoder interfaces, core types, error model, logging hook).
- **FR-007**: The public surface MUST be the only module with public visibility; internal
  modules MUST NOT be reachable from consumer code.
- **FR-008**: The library MUST define an API export macro that yields correct symbol
  visibility for both static and shared-library builds across the supported platforms.
- **FR-009**: A consumer program MUST be able to compile against the library using only the
  umbrella header and the public API, with no reference to internal headers.
- **FR-010**: The utilities and the public surface MUST be independently unit-testable
  (utilities via round-trip/reference tests; public surface via a header-only compile test).

### Key Entities

- **Utilities module**: A foundational helper collection — pixel-format conversion, stride
  computation, PCM conversion. Consumed by backends and examples; depends only on core types.
- **Public umbrella header**: The single include file a consumer uses; re-exports the frozen
  public contracts and nothing internal.
- **API export macro**: Controls symbol visibility for static vs. shared builds per platform.
- **Public contracts (re-exported)**: Encoder interfaces, core media types, error model,
  logging hook — the stable surface consumers code against.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every supported pixel-format and PCM conversion passes automated round-trip or
  reference tests with bit-exact (or documented within-tolerance) results, achieving 100% of
  the utilities unit tests passing.
- **SC-002**: The stride helper returns the correct byte layout for all representative
  widths/formats covered by automated tests, with zero mismatches against expected values.
- **SC-003**: A consumer program that includes only the umbrella header compiles and links
  successfully for both static and shared-library build configurations, using exclusively
  public symbols.
- **SC-004**: The public surface exposes exactly the intended contracts and no internal
  module is reachable from consumer code, verified by an automated include/visibility check.
- **SC-005**: Downstream backends and the encode-to-file example can be built against this
  foundation within the project's normal build, with no temporary utility/export workarounds
  required.

## Assumptions

- The architecture and module-visibility rules from spec `002-architecture-engineering-design`
  (`codec/doc/architecture/module-dependencies.md`, `build-conventions.md`) are authoritative;
  the utilities module depends only on `core`, and only the public module is `//visibility:public`.
- The core types, error model (`Status`/`Result`), `LogSlot`, and abstract encoder
  interfaces referenced here were already implemented in an earlier commit; this proposal does
  NOT re-implement them — it adds the utilities module (a still-missing foundational piece,
  tracked as T008 in `specs/002-.../tasks.md`) and the public umbrella/export surface
  (tracked as T026, not yet implemented).
- Default supported formats for v1 are the common ones needed by the initial backends:
  YUV420P/NV12 for video and the PCM layouts the AAC encoder consumes; exhaustive format
  coverage is out of scope.
- The library targets C++17 with a Bazel build (consistent with spec `001-project-scaffold`);
  success criteria are technology-agnostic but the implementation follows that stack.
- Static library is the primary build; shared-library export behavior is supported via the
  macro but is not the default distribution form for v1.
