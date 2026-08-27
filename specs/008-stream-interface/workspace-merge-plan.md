# Workspace Merge Migration Plan

> **Status**: Proposal — pending separate review/implementation issue.
> This document captures the plan discussed for merging `codec/` and `stream/`
> into a single Bazel workspace, so the whole `video_codec` repo can be consumed
> as one dependency. Execution is NOT started; it will be tracked as a separate
> proposal/issue.

## Problem Statement

The repo root `video_codec` (git: oxsource/video_codec) contains two independent
Bazel workspaces:

| Dir | Workspace name | Notes |
|-----|----------------|-------|
| `codec/`  | `video_codec` | standalone Bazel workspace |
| `stream/` | `video_stream` | depends on codec via `local_repository(path="../codec")` |

`stream/WORKSPACE` also declares `local_repository(name="cpp_network", path="../../cpp_network")`.

**Consumer pain points**:
- An external project cannot consume the whole repo as a unit: `http_archive`
  + `strip_prefix` to `stream/` breaks its `../codec` local_repository (the
  parent dir is not extracted).
- Two workspace names (`@video_codec`, `@video_stream`) must be managed
  separately even though they live in the same git repo.
- The two `local_repository` entries exist only for local debugging; they cannot
  be shared with CI/consumers as-is.

## Decision

Merge `codec/` + `stream/` into a **single Bazel workspace rooted at the repo
root** (`workspace(name = "video_codec")`). `codec/` and `stream/` become
packages within it:

```
video_codec/            # repo root == workspace root
├── WORKSPACE           # workspace(name = "video_codec")
├── .bazelrc            # merged from codec/.bazelrc + stream/.bazelrc
├── .bazelversion       # 6.5.0
├── BUILD.bazel         # root aliases (//:codec, //:stream)
├── deps.bzl            # merged video_codec_deps.bzl + video_stream_deps.bzl
├── platforms/          # unified platform definitions (video_select helper)
├── Makefile + mk/      # merged module system
├── scripts/verify/     # merged verify scripts
├── codec/              # package //codec/... (incl. //codec/tests/...)
└── stream/             # package //stream/... (incl. //stream/third_party/...)
```

cpp_network becomes the **only** external dependency, switchable:
`video_codec_setup(cpp_network_local = False)` → github http_archive
(oxsource/cpp_network) by default; `True` → local `local_repository`
(path = "../../cpp_network") for local debugging.

Label mapping after merge:
- `@video_codec//...` → `//codec/...`
- `@video_stream//...` → `//stream/...`
- codec `//tests` → `//codec/tests`; `//platforms` → `//platforms` (root)
- stream `//src/...` → `//stream/src/...`

## Phased Work Items

### Phase 1 — Root workspace scaffolding
- Create root `WORKSPACE` replacing `codec/WORKSPACE` + `stream/WORKSPACE`:
  `workspace(name = "video_codec")`, `video_codec_setup(...)`, `cc_configure()`,
  `rules_foreign_cc_dependencies(...)`, `rules_android_ndk` + `android_ndk_repository`.
- Merge `.bazelrc` (c++17, caches, platform aliases, android_arm64 NDK
  toolchain, build:shared with both VIDEO_CODEC/STREAM defines).
- Add root `.bazelversion` (6.5.0).
- Create root `platforms/` from codec/platforms; remove stream/platforms;
  unify `video_codec_select`/`video_stream_select` → `video_select`.
- Add root `BUILD.bazel` aliases; adjust `codec/BUILD.bazel` + `stream/BUILD.bazel`.

### Phase 2 — Merged deps.bzl + cpp_network parameterization
- Merge `video_codec_deps.bzl` + `video_stream_deps.bzl` into root `deps.bzl`,
  deduplicating: bazel_skylib, googletest, ffmpeg, rules_foreign_cc, libyuv;
  keep libdatachannel + nlohmann_json.
- Fix `build_file` labels: ffmpeg/libyuv → `//codec/third_party/...`;
  libdatachannel/nlohmann_json → `//stream/third_party/...`.
- `video_codec_setup(cpp_network_local = False)`: default http_archive
  (oxsource/cpp_network, `v1.0.0` tag, sha256 computed during implementation);
  opt-in local_repository.

### Phase 3 — BUILD label rewrite
- codec/ (≈20 files): `@video_codec//` → `//codec/`; `//tests` → `//codec/tests`;
  visibility `@video_stream//` → `//stream/`.
- stream/ (≈11 refs + 11 self-refs + 4 platforms): `@video_stream//` →
  `//stream/`; `@video_codec//` → `//codec/`; `//platforms` → root.
- Remove stale `codec/WORKSPACE`, `stream/WORKSPACE`, per-dir `.bazelrc`,
  `.bazelversion`.

### Phase 4 — Makefile / mk merge
- Single root `Makefile` + `mk/` combining codec (8 modules) + stream (8
  modules); resolve target conflicts:
  - `host-verify` covers `//...` (both codec + stream)
  - `android-build` cross-builds both codec + stream targets
  - keep `build-example` (stream), `dist-publish` (codec shared), `docs-check`
- Merge `scripts/verify/` (dedupe host_build/host_verify/android_build);
  update `codec/scripts/publish.sh` labels → `//codec/src/framework/public:...`.

### Phase 5 — CI merge
- Merge `.github/workflows/`: `ci.yml` (host build `//...` + test
  `//codec/tests/...` + clang-format), `android.yml` (cross-build codec spike +
  stream core/mock), `release.yml` (codec shared publish).
- Remove `stream-ci.yml`; all jobs run from repo root.

### Phase 6 — Docs
- Update live docs: `stream/README.md`, `specs/008-stream-interface/`
  `quickstart.md`, `data-model.md`, `webrtc-backend-context.md` (single-workspace
  labels, cpp_network default github).
- Update drift notes in `plan.md`, `spec.md`, `research.md`, `tasks.md`
  (replace "separate workspace" decision with "merged single workspace").

### Phase 7 — Verification
- `bazel build //...`
- `bazel test //codec/tests/...`
- `make host-verify`
- cpp_network: build with default http; build with `cpp_network_local=True`
- Android cross-build (`--config android_arm64`)

## Open Decisions

1. **cpp_network http pin**: default `v1.0.0` tag (local HEAD is 4 commits
   after). OK, or pin a specific commit?
2. **host-verify / android-build semantics**: cover both codec + stream
   (recommended) vs single module?
3. **stream test.mk**: stream has no `tests/` yet (T053 open) — fold into root
   `test` (only `//codec/tests/...`) or drop the placeholder targets?

## Outcome

One `http_archive`/`local_repository` on `oxsource/video_codec` exposes both
`//codec/...` and `//stream/...`; the only external repo is cpp_network
(github by default, local for debugging), matching the original "参数化、默认
不用 local" requirement without the separate-workspace drawbacks.