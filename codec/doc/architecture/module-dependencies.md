# Module Dependencies & Visibility

## Dependency graph

```
public ──▶ api ──▶ core
   ├──▶ utils ──▶ core
   └──▶ backend/* ──▶ api, core, utils, (external dep)
```

- `core` depends on **nothing** (leaf).
- `api` → `core` only.
- `utils` → `core` only.
- Each `backend/<x>` → `api`, `core`, `utils`, **and exactly one** external dependency
  (`@ffmpeg` / `@androidndk` / `VideoToolbox.framework`).
- `backends` NEVER depend on each other.
- `public` → all of the above; it is the **only** externally-visible package.

## Visibility rules

| Package | `visibility` |
|---------|--------------|
| `core`, `api`, `utils`, `backend/*` | `["//src/framework:__subpackages__", "//tests:__subpackages__"]` |
| `public` | `["//visibility:public"]` |
| `third_party/ffmpeg`, `third_party/android_ndk` | `["//visibility:public"]` (only `backend/*` may consume) |

## Invariants (CI-enforced)

1. No edge may point **out** of `core` (it is a leaf).
2. No edge between two `backend/*` packages.
3. Only `public` may be depended on by non-`src/framework` code.
4. A backend may only reference its own external dep; e.g. `backend/ffmpeg` must not
   reference `@androidndk`, and vice-versa.

A Bazel visibility query in CI (`bazel query`) can assert (3); (1)/(2) are structural and
reviewed in PRs. See [build-conventions.md](../build-conventions.md).
