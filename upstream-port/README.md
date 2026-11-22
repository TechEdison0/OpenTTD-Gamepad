# Vendored porting glue

This directory records the third-party build glue that makes this fork build and
run on Android, and how it was derived. It exists so the glue can be audited,
re-applied and rebased instead of being baked into `main` as opaque edits.

## Contents

| File | Description |
|---|---|
| `psyycker-rc1.patch` | Exact diff applied on top of upstream `16.0-RC1` to add the Android build glue and the minimum engine changes needed to launch. Binary files are included (`--binary`). |

## Provenance

| Field | Value |
|---|---|
| Donor | `https://github.com/psyycker/openttd-android` |
| Donor revision | `9407a7f` (GPL-2.0-only) |
| Base | OpenTTD `16.0-RC1` = `07b339c0aaeb0a87be9490b2f1c3f17c6b00b837` |
| Imported | 2026-11-22 |

## Regenerating

```bash
git clone --filter=blob:none https://github.com/psyycker/openttd-android.git vendor/psyycker
git -C vendor/psyycker fetch --depth 1 origin 07b339c0aaeb0a87be9490b2f1c3f17c6b00b837
git -C vendor/psyycker diff --binary 07b339c0aa HEAD > upstream-port/psyycker-rc1.patch
```

## Adaptations made on import

- Removed `.github/README.md` and `.github/workflows/deploy.yml` (donor-specific
  landing page and release automation).
- Rewrote `os/android/README.md` for this fork (branch model, releases).
- Kept `LICENSE` (GPL-2.0 text); the engine remains GPL-2.0 under `COPYING.md`.
- Added explicit `<chrono>`, `<cstdint>` and `<vector>` includes to
  `src/video/sdl2_touch.h`; the donor relied on transitive includes that this
  tree does not provide (Linux build failed on `Clock`/`std::vector`).
- Pinned the Android CMake to the SDK package `cmake;3.31.6`
  (`os/android/app/build.gradle.kts`) so the host's `/usr/bin/cmake` cannot shadow
  it; documented `cmake.dir` accordingly.
- Everything else was applied verbatim; later local fixes are listed here as they
  are made.

## What the patch touches

Additive glue: `os/android/**` (Gradle/NDK/vcpkg project, SDL Java classes, app
activities), `src/os/android/**`, `src/video/sdl2_touch.{cpp,h}`,
`src/video/opengl_es.h`, `cmake/HostTools.cmake`, and CMake wiring.

Engine changes: OpenGL ES support in `src/video/opengl.cpp` behind `_gl_es`
(desktop behaviour unchanged), Android branches in `src/video/sdl2_v.cpp` /
`sdl2_v.h`, and generic input helpers in `src/window.cpp`
(`HandleBackButton`, `HandleTouchPan`, `HandleJoystickScroll`, `HandleTouchZoom`).
See `THIRD_PARTY_NOTES.md`.

## Rebasing onto a new upstream release

1. `git fetch upstream <release-tag>`
2. `git rebase <release-tag>` and resolve conflicts in the patched engine files
   (expect `opengl.cpp`, `sdl2_v.cpp`, `window.cpp`).
3. If the donor has moved, regenerate this patch and re-apply it as a separate
   commit so the rebase stays reviewable.
