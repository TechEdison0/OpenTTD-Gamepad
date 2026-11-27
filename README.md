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
under Wine, Android phones such as the Galaxy S25 Ultra). Published pre-releases are
compiled with `-DGAMEPAD_DEBUG_OFF`: they ship no controller diagnostics window, and no
survey/telemetry data is collected or transmitted. Do not use this fork for anything you
care about losing; use upstream releases for that.

Base pin: upstream **`16.0-RC1`** (`07b339c0aaeb0a87be9490b2f1c3f17c6b00b837`),
rebased onto future upstream release tags only — never nightlies.

## Features

1. **Desktop (Windows / Linux)** — discover gamepads, normalize input, navigate
   the UI and play from a controller without breaking keyboard/mouse. The active input
   method switches automatically based on what you touch.
2. **Android, single screen** — real touch interface, plus gamepad and kb/m.
3. **Android, dual screen (handhelds)** — the entire toolbar lives on the bottom
   display and the map on the top. Sticks drive the viewport/crosshair on the map;
   the D-pad drives menu focus on the tools screen. Layout is computed at runtime and
   re-assembled on dock/undock (a restart prompt is acceptable).

UI behaviour follows controller conventions from Cities: Skylines, Civilization and
similar top-down strategy games.

Controller implementation: discovery → normalization → binding → dispatch in
`src/gamepad/`, a red focus ring drawn by OpenTTD's renderer, geometric D-pad navigation
over live widget geometry, toolbar A activation and B back-out.

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
- **Android** — compiles FluidSynth natively into the APK and ships the soundfont.

## Repository layout

| Path | Purpose |
|---|---|
| `src/` | engine, plus this fork's controller additions |
| `src/gamepad/` | controller subsystem: SDL discovery/hotplug, device registry, `controller.cfg` persistence |
| `src/controller_gui.*` | standalone Controller settings window and its main-menu entry |
| `os/android/` | Gradle/NDK/vcpkg Android application project |
| `upstream-port/` | vendored additive build glue, the Wine launcher, provenance and license notes |
| `THIRD_PARTY_NOTES.md` | third-party components and where they come from |
| `.github/workflows/ci-desktop.yml`, `ci-android.yml` | the fork's own build checks |

Upstream is tracked on the `upstream` remote; `main` is the integration branch,
rebased onto new upstream release tags. Design research and the decision log live in
the workspace `research/` directory next to this clone, not inside the repository.

## Versions and downloads

Pre-releases are **test snapshots**, not a product channel. Each tag pins an exact
commit of this repository; binaries and source are published together.

Every build is pinned to:

- **OpenTTD `16.0-RC1`** (`07b339c0aaeb0a87be9490b2f1c3f17c6b00b837`) — the upstream
  engine at that tag, essentially unmodified;
- **`psyycker/openttd-android`** @ `9407a7f` — the Android porting glue **as published,
  before any modification by this fork**, vendored under `upstream-port/`;
- this fork's own commits on top: build/packaging wiring, audio fixes, Windows SDL
  linkage for controller input, the Controller window, the device registry and
  `controller.cfg`, and removal of the startup survey.

### Current release — C6 axis configuration menu

`v20261127-c6-axis-configuration-menu` adds per-controller axis configuration. The
controller menu is now a vertical stack with Guide, Controllers & Mapping and Controller
Options. Invert toggles, deadzone percentages, slow/normal/fast stick speeds and stick
swap are stored per controller in `controller.cfg` and applied by the input pipeline.
Rows in the options window are gamepad-focusable. The maintainer verified all functions
on Linux x86_64, Windows/Wine and Android Galaxy S25 Ultra.

### Previous release — C5 navigation refinements

`v20261127-c5-navigation-refinements` refines controller navigation for specific widgets
and edge cases. Town Directory town rows are focusable and activate with A, and road/rail
station direction panels are reachable with the D-pad. Initial focus prefers foreground
windows while directional navigation stays geometric, so picker sub-windows such as roads,
rails and terrain no longer snap back to the top toolbar. Slider and Advanced Options
accordion editing stays on cursor control.

The maintainer verified the changes on Linux x86_64, Windows/Wine and Android Galaxy S25
Ultra.

### Previous release — C4 viewport movement

`v20261127-c4-viewport-movement` adds right-stick viewport pan. The right stick scrolls
the main viewport through the existing joystick-scroll path with the shared 15% radial
deadzone and gamma-2 response curve. It is independent of D-pad focus and the left-stick
crosshair, so a build tool window can stay focused while the map moves and placement is
still controlled with the left stick and A.

The maintainer verified simultaneous left/right-stick use on Linux, Windows/Wine and
Android Galaxy S25 Ultra.

### Previous release — C3 controller focus navigation

`v20261127-c3-controller-focus` completes the D-pad focus model. The red focus ring
highlights one live widget at a time, D-pad navigation moves geometrically across windows,
A activates the focused widget, and B closes through the normal back path. Dropdown rows
are individually focusable without cursor-driven auto-selection. Focus can reach toolbar
build windows and windows opened by clicking viewport objects such as depots, vehicles and
towns; main-menu controls behind an open dialog are suppressed. The bottom status bar is
ignored, and the main toolbar save button is focusable. Returning from left-stick crosshair
control resumes at the last focused D-pad item.

The maintainer verified a controller-only workflow on all three targets: created a depot
and two bus stops, bought a bus, assigned orders, and cloned the bus with shared orders.

### Previous release — C2 playable controller layer

`v20261126-c2-action-layer` is the first **playable** controller pass. The pad can move
the crosshair across the whole UI and perform the functions needed to play, including held
Ctrl/Shift modifiers for tools and build actions. The main-menu **Controller** entry still
opens the launcher with **Controller Guide** and **Controllers & Mapping**; the in-game
options cog also has a **Controller Guide** entry directly below Game Options. The guide is a plain `Button -> Description` list with sprite bullets and
built-in arrow glyphs; the mapping window shows `Control`, `Mapping` and `Live` columns
with the current value of every mapped input.

The mapping flow has **18 steps**: buttons, D-pad directions, stick axes and triggers.
Back/Select/Start/Guide are intentionally absent because Android devices claim them as
system buttons; L3 pauses and R3 toggles fast forward.

Controllers SDL already maps use its normalized **logical axes**: sticks near zero with
a `-32768..32767` range and triggers `0..32767`, so standard pads keep the simple
one-move-per-prompt flow. Raw or unmapped controllers use an explicit calibration gesture
and edge-triggered capture from a learned rest/range instead of assuming every axis rests
at zero. In both paths, a captured axis must move away from rest before it can satisfy a
prompt; after capture, axis events are ignored for 500 ms so one gesture cannot map two
axes.

Captured values are stored per controller in `controller.cfg` beside `hotkeys.cfg`
(`button:N`, `axis:N:min:max:rest`, `logical:N:min:max:rest`, `hat:H:bits`) and reloaded on
start; finishing the flow auto-saves. Gyroscope/accelerometer-style axes are excluded.

C2 adds normalized, press-and-release actions with no synthetic SDL events: A is a held
left click, X is a held right click, Y cancels the active tool, LB/RB are held Ctrl/Shift,
LT/RT zoom one step per press, left stick moves the crosshair through a scaled radial
deadzone and gamma-2 curve, L3 pauses and R3 toggles fast forward. Real keyboard/mouse
activity takes priority; held pad buttons survive crosshair movement and release on the
physical button.

Run it: Linux `./openttd`; Windows double-click `wine_launch.sh` (applies the Wine
gamepad registry flags, video stays on win32); Android install the APK. Base
graphics/sound/music sets are not included.

| Release | Artifacts |
|---|---|
| `v20261127-c6-axis-configuration-menu` | C6 controller options: per-controller invert, deadzone, stick speed and stick swap |
| `v20261127-c5-navigation-refinements` | C5 navigation refinements: town rows, station direction panels, picker sub-window focus |
| `v20261127-c4-viewport-movement` | C4 right-stick viewport pan, independent of D-pad focus and left-stick placement |
| `v20261127-c3-controller-focus` | C3 focus navigation: red widget ring, D-pad window/dropdown navigation, focused A activation, B close, crosshair handoff |
| `v20261126-c2-action-layer` | C2 playable controller pass: full UI navigation, held A/X, Ctrl/Shift modifiers, crosshair, zoom, pause/fast-forward |
| `v20261126-c1-controller-implementation` | C1 controller layer: launcher, function guide, 18-step mapping, live values |
| `v20261124-c0-gamepad-test` | controller event verification (diagnostics window) |
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
