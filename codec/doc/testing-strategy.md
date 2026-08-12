# Testing Strategy

Framework: **googletest 1.14.0** (pinned in `video_codec_deps.bzl`). Three tiers.

## Tiers

### 1. Unit (`tests/`, `core` + `utils`)
Pure-logic, fast, platform-independent.
- `core_test.cc`: frame/packet/config construction, `StatusCode`/`Result<T>` behavior.
- `utils_test.cc`: pixel format round-trips (YUV420P ↔ NV12, stride alignment), sample
  format round-trips (interleaved ↔ planar, s16 ↔ f32). Assert lossless round-trip.

### 2. Integration (`backend/*`)
Encode a few frames; assert the output decodes. Gated behind the backend's platform.
- `backend/ffmpeg`: encode N frames → write Annex-B → `ffprobe` decodes as the expected
  codec/width/height (golden: 320×240 H.264, ~14778 bytes from the known spike input).
- `backend/android`: requires a device/emulator + NDK; asserts `mediacodec_spike`-style
  output decodes on-device. Skipped on host.

### 3. Smoke (spikes)
`src/spike/ffmpeg_spike` and `src/spike/mediacodec_spike` double as end-to-end smoke
tests. `make verify` runs the FFmpeg spike + `ffprobe` assertion.

## Structure

```text
tests/
├── core_test.cc
├── utils_test.cc
└── encoder_test.cc      # backend smoke test with mock frames (select()-gated)
```

## Commands

```bash
bazel test //...                 # all tiers the current platform supports
make verify                      # host: build + ffmpeg_spike + ffprobe golden check
```

## Golden checks

Video golden = `ffprobe` reports `codec_name`/`width`/`height` matching the config, and
byte size is within an allowed drift of the reference (libx264 output is deterministic
for fixed params). Audio golden = ADTS header parses and frame count matches.
