# Error Handling

## Principle

**No exceptions cross the public API.** Every fallible operation returns either a
`Status` (for `void`-like ops) or a `Result<T>` (for value-returning ops). Internal
code may use exceptions for control flow, but it converts to `Status` at the public
boundary.

## Status

```cpp
enum class Status {
    kOk = 0,
    kInvalidArgument,
    kNotInitialized,
    kEncodeFailed,
    kUnsupportedFormat,
    kBackendUnavailable,
    kPlatformUnsupported,
    kUnsupportedOperation,
};
```

## Result<T>

```cpp
template <typename T>
class Result {
  public:
    static Result Ok(T v);
    static Result Error(Status c);
    bool ok() const;
    Status status() const;       // kOk when holding a value
    const T& value() const;          // caller checks ok() first
};
```

## Propagation rules

- A backend maps its external encoder's error to the nearest `Status`
  (FFmpeg `AVERROR`, NDK `AMEDIACODEC_*` → `kEncodeFailed`/etc.).
- `Create()` returns `nullptr` (not an exception) when no backend matches the platform.
- Optional operations (`CreateInputSurface` on FFmpeg) return `nullptr` / empty; the
  `Encode(NativeBuffer)` variant returns `kUnsupportedOperation` rather than throwing.
- Logging of errors goes through `LogSlot` (see [logging-slot.md](logging-slot.md)).

## Why not exceptions

- The library may be consumed across a C boundary and from NDK/JNI; cross-boundary
  exceptions are unsafe and often disabled in embedded toolchains.
- `Status` is ABI-stable and trivially interop-able from other languages.
