<!-- SPECKIT START -->
For additional context about technologies to be used, project structure,
shell commands, and other important information, read the current plan:
specs/004-encoder-queue-wiring/plan.md (Implementation Plan), and the design
doc at codec/doc/project_bootstrap.md. Architecture & engineering design (module
dependencies, lifecycle, backend selection, threading, output queue + consumer (file/stream), error handling, logging, ADRs)
lives under codec/doc/architecture/ and codec/doc/adrs/. The active feature (spec 004)
wires the encoder to the output queue: optional OutputSink push mode on the encoders,
so encoded packets flow into the queue automatically (pull API stays the default).
<!-- SPECKIT END -->
