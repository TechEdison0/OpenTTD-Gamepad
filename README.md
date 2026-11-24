# OpenTTD Gamepad

> **AI-use disclaimer.**
> This fork was created with substantial assistance from AI coding agents: research,
> design notes, code changes and documentation were produced or drafted with AI
> tooling and reviewed and tested afterwards by the maintainer. It is an
> **experimental, community-unverified fork** — expect rough edges, untested code
> paths and behaviour that differs from upstream. It is provided as-is, without any
> warranty, and is **not affiliated with, sponsored by, or endorsed by the OpenTTD
> team**.

OpenTTD is a transport simulation game based on *Transport Tycoon Deluxe* by Chris
Sawyer. For the game itself, the manual, supported platforms, download links and all
other general documentation, see the
**[upstream OpenTTD README](https://github.com/OpenTTD/OpenTTD/blob/master/README.md)**.
Everything below describes only what this fork adds.

[![Desktop build](https://github.com/TechEdison0/OpenTTD-Gamepad/actions/workflows/ci-desktop.yml/badge.svg?branch=main)](https://github.com/TechEdison0/OpenTTD-Gamepad/actions/workflows/ci-desktop.yml?query=branch%3Amain)
[![Android build](https://github.com/TechEdison0/OpenTTD-Gamepad/actions/workflows/ci-android.yml/badge.svg?branch=main)](https://github.com/TechEdison0/OpenTTD-Gamepad/actions/workflows/ci-android.yml?query=branch%3Amain)
![License](https://img.shields.io/badge/license-GPL--2.0--only-blue)

---

## What this fork is

A fork of [OpenTTD](https://www.openttd.org/) that makes a controller a first-class
input method everywhere OpenTTD runs, then builds the Android handheld experience on
top of that.

It exists because one person wanted those features for their own handhelds; publishing
it is a bonus for anyone else who wants them too.

**It is not intended for upstream.** No pull requests are planned, for two reasons: the
AI-assisted provenance would make upstream review inappropriate, and the core goal
(handheld dual-screen input) is outside the platform set the parent project targets.
This is a separate, self-contained distribution.

**Maturity:** experimental and generally unverified. Builds are reproducible from this
repository, but device coverage is limited (desktop Linux, the x86_64 Windows build
under Wine, Android phones such as the Galaxy S25 Ultra). Test builds additionally show
a **Controller diagnostics** window with raw gamepad events. Do not use this fork for
anything you care about losing; use upstream releases for that.

Base pin: upstream **`16.0-RC1`** (`07b339c0aaeb0a87be9490b2f1c3f17c6b00b837`),
rebased onto future upstream release tags only — never nightlies.

## Features

1. **Desktop (Windows / Linux / macOS)** — discover gamepads, normalize input, navigate
   the UI and play from a controller without breaking keyboard/mouse. The active input
   method switches automatically based on what you touch.
2. **Android, single screen** — real touch interface, plus gamepad and kb/m.
3. **Android, dual screen (handhelds)** — the entire toolbar lives on the bottom
   display and the map on the top. Sticks drive the viewport/crosshair on the map;
   the D-pad drives menu focus on the tools screen. Layout is computed at runtime and
   re-assembled on dock/undock (a restart prompt is acceptable).

UI behaviour follows controller conventions from Cities: Skylines, Civilization and
similar top-down strategy games.

Planned engine shape (not yet implemented): platform-neutral action layer in core
(`src/`) — discovery → normalization → binding → dispatch, with thin per-platform
backends; a focus ring drawn by OpenTTD's own renderer; geometric D-pad navigation
using live widget geometry; toolbar A activates / B backs out.

## Building

Linux:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
```

Android (JDK 17+, SDK/NDK, CMake ≥ 3.31, Ninja, vcpkg):

```bash
cd os/android && ./gradlew assembleDebug
```

Windows produces a statically linked `openttd.exe` via a MinGW cross-build. It needs
the **POSIX-threads** MinGW variant (Ubuntu's default win32-threads toolchain cannot
compile `<condition_variable>`) plus a native host-tools build, so it is scripted
rather than a one-liner:

```bash
sudo apt install -y gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix
```

The exact, current recipes for all three platforms live in the maintainer's build notes
(`research/DECISIONS.md` and the project skill); the short version is above.

See also `COMPILING.md`, `os/android/README.md`, `THIRD_PARTY_NOTES.md`,
`upstream-port/README.md` and `upstream-port/LICENSES.md`.

## Runtime dependencies

Only **Linux** needs anything beyond base system packages: music requires a
General-MIDI synthesizer plus a GM soundfont. Sound effects work without either.

| Debian/Ubuntu package | Purpose |
|---|---|
| `libfluidsynth3` | FluidSynth software synthesizer (runtime) |
| `timgm6mb-soundfont` (or `fluid-soundfont-gm`) | General-MIDI soundfont |

```bash
sudo apt install libfluidsynth3 timgm6mb-soundfont
```

Building from source additionally needs `libfluidsynth-dev`. Without FluidSynth the
build falls back to `extmidi` (an external player such as `timidity`) or silence.

The other platforms need nothing extra:

- **Windows** — FluidSynth compiled in via vcpkg, with `soundfont.sf2` shipped next to
  `openttd.exe`. Works even where the OS MIDI mapper has no synthesizer (e.g. Wine).
- **macOS** — uses CoreAudio's built-in synthesizer (`cocoa_midi`).
- **Android** — compiles FluidSynth natively into the APK and ships the soundfont.

## Repository layout

| Path | Purpose |
|---|---|
| `src/` | engine, including the (planned) core gamepad action/focus layer |
| `src/os/android/` | Android native entry point |
| `os/android/` | Gradle/NDK/vcpkg Android application project |
| `upstream-port/` | vendored additive build glue, its provenance and license notes |
| `THIRD_PARTY_NOTES.md` | third-party components and where they come from |
| `.github/workflows/ci-desktop.yml`, `ci-android.yml` | the fork's own build checks |

Upstream is tracked on the `upstream` remote; `main` is the integration branch,
rebased onto new upstream release tags. Design research and the decision log live in
the workspace `research/` directory next to this clone, not inside the repository.

## Roadmap

- [x] Baseline build and packaging for Linux, Windows and Android
- [x] Working audio on all three (SFX + MIDI music)
- [x] Controller signal path: SDL discovery/hotplug and an on-screen diagnostics window (test builds)
- [ ] Core input: device model, normalization, bindings, action dispatcher
- [ ] Controller foundation: focus ring, geometric D-pad, toolbar A/B
- [ ] Dropdowns, build toolbars, on-screen keyboard under controller focus
- [ ] Android single-screen touch/gamepad/kb-m parity
- [ ] Android dual-screen regions and context-sensitive input routing
- [ ] Device testing, polish, rebase rehearsal

## Versions and downloads

Pre-releases are **test snapshots**, not a product channel. Each tag pins an exact
commit of this repository; binaries and source are published together.

Every build is pinned to:

- **OpenTTD `16.0-RC1`** (`07b339c0aaeb0a87be9490b2f1c3f17c6b00b837`) — the upstream
  engine at that tag, essentially unmodified;
- **`psyycker/openttd-android`** @ `9407a7f` — the Android porting glue **as published,
  before any modification by this fork**, vendored under `upstream-port/`;
- this fork's own commits on top: build/packaging wiring, audio fixes, Windows SDL
  linkage for controller input, and the controller probe described below.

### Current test release — controller event verification

First release carrying a gamepad feature: raw controller events are captured by a
shared SDL backend and shown in a native OpenTTD window, **Controller diagnostics**,
which lists connected devices (name, SDL GUID, axes/buttons/hats) and streams
connect/disconnect/axis/button/hat events live. The window opens automatically in these
test builds; there is no configuration step.

Run it: Linux `./openttd`; Windows double-click `wine_launch.sh` (applies the Wine
gamepad registry flags and selects the SDL backend); Android install the APK.

Verified on desktop Linux, the x86_64 Windows build under Wine 9.0, and a Samsung
Galaxy S25 Ultra. Waydroid can host the app for layout checks but does not forward
Bluetooth gamepads.

Known limits: mappings/bindings and D-pad navigation are not implemented yet; the
window is scaffolding for that work and disappears from release builds compiled with
`-DGAMEPAD_DEBUG_OFF`. Base graphics/sound/music sets are not included.

| Release | Artifacts |
|---|---|
| `v20261124-c0-gamepad-test` | Linux x86_64 (folder + `.zip`), Windows x86_64 (folder + `.zip`, includes `wine_launch.sh`), Android `arm64-v8a` debug APK |
| `v20261123-rc1-static-test` | earlier snapshot: RC1 engine + donor Android glue, no gamepad code |

Downloads are attached to the
[GitHub releases page](https://github.com/TechEdison0/OpenTTD-Gamepad/releases).
There is no updater and no nightly channel: download a file, run it, report what breaks.

## Credits and licensing

**OpenTTD** is Copyright © the OpenTTD developers and contributors and is licensed
under the **GNU General Public License, version 2.0** (GPL-2.0-only). The complete
license text ships here as `COPYING.md`; upstream's copy and the full list of
engine-bundled third-party components (squirrel, fmt, nlohmann-json, catch2, ICU
scriptrun, monocypher, and others) are in the upstream README's "Licensing" section.
OpenTTD and its logo are the property of the OpenTTD project; this fork is not
affiliated with or endorsed by it. All modifications made by this fork are released
under the same GPL-2.0-only terms.

**Material added by this fork** is listed in
[`THIRD_PARTY_NOTES.md`](THIRD_PARTY_NOTES.md), with the per-component compliance
matrix in [`upstream-port/LICENSES.md`](upstream-port/LICENSES.md). In summary:

| Component | License | Where |
|---|---|---|
| OpenTTD engine | GPL-2.0-only | `COPYING.md` |
| psyycker/openttd-android build glue | GPL-2.0-only | vendored under `upstream-port/` |
| TimGM6mb General-MIDI soundfont | GPL-2 | `soundfont.sf2` in the APK and the Windows package |
| SDL Java/bootstrap classes | zlib | `os/android/app/src/main/java/org/libsdl/` |
| Build dependencies linked statically (SDL2, FreeType, HarfBuzz, ICU, curl, OpenSSL, libpng, zlib, liblzma, lzo, soxr, opusfile, FluidSynth, …) | their own; see LICENSES.md | vcpkg / distro packages |

**Game content is not included.** OpenGFX, OpenSFX, OpenMSX, NewGRFs, AIs and game
scripts retain their own licenses and are downloaded separately.

**Source availability.** Corresponding source for everything shipped in this fork's
binaries is available from this repository — each package names its exact commit — and
from the upstream projects of each GPL component under the terms of their licenses.
