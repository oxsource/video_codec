# Contract: Public API Surface

**Owner**: `src/framework/public` | **Consumers**: any Bazel target depending on
`@video_codec//src/framework/public:video_codec` (or the shared library).

This contract freezes the public C++ surface. Implementation must not add, remove, or
change the signature of any symbol below without a version bump (see
`codec/doc/release-process.md`).

## Namespace

```cpp
namespace video {
namespace codec {
// ... public types ...
}  // namespace codec
}  // namespace video
```

## Umbrella header

`#include "video_codec/video_codec.h"` pulls `core.h` + `api.h` + `utils.h`. All public
symbols are exported via `VIDEO_CODEC_API` (defined in `video_codec_export.h`; the
shared-library build defines `VIDEO_CODEC_SHARED_LIBRARY`).

## Exported types (frozen)

- Enums: `VideoCodecType`, `AudioCodecType`, `PixelFormat`, `SampleFormat`,
  `BitrateMode`, `Backend`, `Status`.
- Values: `VideoFrame`, `AudioFrame`, `Packet`, `NativeBuffer`,
  `VideoEncoderConfig`, `AudioEncoderConfig`.
- Error: `Status`, `Result<T>`.
- Interfaces: `VideoEncoder`, `AudioEncoder`, `InputSurface`.
- Logging: `LogSlot`, `SetLogSlot(LogSlot*)`.

## Factory functions (frozen)

```cpp
namespace video::codec {
  // Returns nullptr on unsupported platform/backend; never throws.
  VIDEO_CODEC_API std::unique_ptr<VideoEncoder>
      VideoEncoder::Create(const VideoEncoderConfig& config);

  VIDEO_CODEC_API std::unique_ptr<AudioEncoder>
      AudioEncoder::Create(const AudioEncoderConfig& config);
}
```

- `Create` resolves the backend via `ResolveBackend(config.backend)` (see
  `data-model.md` §6). On an unsupported combination it returns `nullptr`
  (not an exception).
- The returned object satisfies the `encoder-contract.md`.

## ABI rules

- No `std::string` / `std::vector` cross the boundary by value in the *public* surface
  except the value types defined here (which are self-contained POD-ish structs).
- All fallible calls return `Status` / `Result<T>`; **no exceptions escape**.
- The shared library is built with `-fvisibility=hidden`; only `VIDEO_CODEC_API`-marked
  symbols are exported.
