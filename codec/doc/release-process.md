# Release Process

The library is consumed as a Bazel dependency (`@video_codec//src/framework/public:video_codec`)
and optionally as a shared library.

## Versioning

- Semantic versioning `MAJOR.MINOR.PATCH`.
- **MAJOR** bumps when a `public-api.md` symbol is added/removed/changed in signature.
- **MINOR** bumps for new backends / codecs / non-breaking additions.
- **PATCH** bumps for bug fixes.
- Version is recorded in `CHANGELOG.md` and (optionally) a generated `version.h`.

## Publishing

1. Implement + land on `main` via PR (CI green: macOS + Linux + Android).
2. Update `CHANGELOG.md` with the new version and notable changes.
3. Tag: `git tag vX.Y.Z` → pushes the `release.yml` workflow.
4. `release.yml` builds the shared library for the supported targets and publishes the
   Bazel archive / artifact.

## Compatibility promise

- The C++ public API (`public-api.md`) is the stability boundary. Anything under
  `src/framework/{core,api,utils,backend}` not exported via `VIDEO_CODEC_API` may change
  in a MINOR.
- `Status` values are append-only (new codes may be added; existing codes keep their
  meaning) to preserve ABI for consumers that switch on them.

## CHANGELOG

`CHANGELOG.md` at repo root, populated on each release. Initial scaffold entry is the
baseline (see repo root `CHANGELOG.md`).
