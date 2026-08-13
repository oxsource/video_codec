<!-- SPECKIT START -->
For additional context about technologies to be used, project structure,
shell commands, and other important information, read the current plan:
specs/005-muxer-encoder-layering/plan.md (Implementation Plan), and the design
doc at codec/doc/project_bootstrap.md. Architecture & engineering design (module
dependencies, lifecycle, backend selection, threading, output queue + consumer (file/stream), muxer layering, error handling, logging, ADRs)
lives under codec/doc/architecture/ and codec/doc/adrs/. The active feature (spec 005)
introduces a generic Muxer interface in `api` (peer of VideoEncoder/AudioEncoder,
reference Android MediaCodec): it implements PacketSink so a queue's Await() hands
packets straight to it, with the first implementation in the FFmpeg backend (mux/
module and consumer/Mp4Consumer are removed).
<!-- SPECKIT END -->
