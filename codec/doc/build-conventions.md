# Build Conventions

Bazel 6.5.0. Mirrors the native_ui reference project.

## Rules

1. **One `BUILD.bazel` per directory.** Each module and each `backend/*` subdirectory is
   self-contained.
2. **Visibility**: internal packages use
   `["//src/framework:__subpackages__", "//tests:__subpackages__"]`; only `public` is
   `//visibility:public`.
3. **Dep prefix for select()**: backend selection uses `select({...})` keyed by
   `//platforms:<name>`; never hardcode a platform in a `deps` list.
4. **External deps**: declared once in `video_codec_deps.bzl` and referenced by label
   (`@ffmpeg`, `@androidndk`, `@com_google_googletest`, `@bazel_skylib`). Do not add
   `http_archive` outside that file.
5. **FFmpeg**: built from source via `rules_foreign_cc` `configure_make`
   (`third_party/ffmpeg/BUILD.bazel`); consumed as `//third_party/ffmpeg:ffmpeg_codec`
   (static, force-loaded). Do not `glob` FFmpeg sources.
6. **alwayslink + force_load**: the `ffmpeg_codec` `cc_library` is `alwayslink = True`
   and the spike adds `-Wl,-force_load,$(execpath ...ffmpeg_codec_archive)`; keep both.
7. **C++17**: `build --cxxopt=-std=c++17` (in `.bazelrc`); no C++20 features.
8. **No `//...` surprises**: `mk/` and `scripts/` have no `BUILD` file, so they are not
   packages and are ignored by `bazel build //...`.

## Naming

- Targets: `lower_snake_case` (`core`, `api`, `ffmpeg`, `video_codec`).
- `cc_library` per directory; `cc_binary` only for spikes/examples.
- Test targets: `<name>_test` under `tests/`.

## Formatting / lint

- clang-format (Google style, 2-space indent) and clang-tidy on `src/`.
- CI enforces both (see [ci-strategy.md](ci-strategy.md)).
