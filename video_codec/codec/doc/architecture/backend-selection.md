# Backend Selection

## At runtime (factory)

`VideoEncoder::Create` / `AudioEncoder::Create` call `ResolveBackend(backend)`:

```cpp
Backend ResolveBackend(Backend force) {
    if (force != Backend::kAuto) return force;        // explicit override
#if defined(__ANDROID__)
    return Backend::kAndroid;
#elif defined(__APPLE__)
    return Backend::kFFmpeg;   // VideoToolbox reserved → fallback (ADR-004)
#else
    return Backend::kFFmpeg;
#endif
}
```

- `kAndroid` → `MediaCodecVideoEncoder` / `MediaCodecAudioEncoder`
- `kFFmpeg` / `kAuto` (non-Android) → `FFmpegVideoEncoder` / `FFmpegAudioEncoder`
  (Apple falls back to FFmpeg; a native Apple backend is reserved — ADR-004)

`Create` returns `nullptr` when the resolved backend is unavailable on the current
platform (e.g. `kAndroid` requested on a desktop build).

## At link time (`select()`)

Only the chosen backend — and its external dependency — is linked. In
`src/framework/public/BUILD.bazel`:

```python
deps = [":core", ":api", ":utils"] + select({
    "//platforms:android_arm64": ["//src/framework/backend/android"],
    "//platforms:darwin_arm64":  ["//src/framework/backend/ffmpeg"],  # VT reserved
    "//conditions:default":      ["//src/framework/backend/ffmpeg"],
})
```

Consequences:
- A non-Android build never pulls `@androidndk` (host stays NDK-free).
- A non-desktop build never pulls `@ffmpeg` unless explicitly selected.
- `backend` does **not** change the link; it changes only the runtime choice and
  is intended for debug/test on a build that already links the target backend.

## `backend` use

Set in `VideoConfig`/`AudioConfig` to override platform auto-selection
(e.g. test the FFmpeg backend on Android, or the Android backend in an emulator build).
It must match a backend the binary actually links, or `Create` returns `nullptr`.
