# License & Distribution Compliance

This fork is a **derivative work of OpenTTD**, which is licensed under the
**GNU General Public License, version 2.0 only (GPL-2.0-only)**. Everything this
fork adds is released under the same terms, so **binary distributions built from
this repository must comply with GPL-2.0**: keep the license text with the binary,
do not add restrictions, and make the corresponding source available.

This file is the practical checklist. `THIRD_PARTY_NOTES.md` records provenance;
the actual license texts live next to each component (see below).

## What every package must contain

| File | Why |
|---|---|
| `COPYING.md` | GPL-2.0 text, as shipped by OpenTTD |
| `LICENSE.txt` (or `LICENSES.md`) | this compliance summary plus the added component list |
| `README.txt` | package-specific notes |

Every package names its exact commit, so the corresponding source is that commit of
this repository. GPL §3 obligations (written offer / network distribution) are met
by publishing both the binaries and their source in the same place.

## 1. Engine

| Component | License | Notes |
|---|---|---|
| OpenTTD engine (`src/`, `bin/`, `lang/`, `ai/`, `game/`, `baseset/` scaffolding) | GPL-2.0-only | Copyright © the OpenTTD developers. Text in `COPYING.md`. |

Components bundled **inside** the engine tree have their own licenses (squirrel =
Zlib, md5 = Zlib, fmt = MIT, nlohmann-json = MIT, OpenGL headers = MIT, catch2 =
BSL-1.0, icu scriptrun = Unicode, monocypher = BSD-2-Clause/CC0, social integration
API = MIT, LLVM `CheckAtomic.cmake` = Apache-2.0). These are unmodified upstream
bundles; the authoritative list is the "Licensing" section of the
[upstream README](https://github.com/OpenTTD/OpenTTD/blob/master/README.md) and the
license files next to each directory. All are GPL-2.0-compatible.

## 2. Code added by this fork

| Component | License | Source |
|---|---|---|
| `psyycker/openttd-android` Gradle/NDK/vcpkg glue, vendored under `os/android/` and `src/os/android/` | GPL-2.0-only | https://github.com/psyycker/openttd-android @ `9407a7f`; delta recorded in `upstream-port/psyycker-rc1.patch` |
| SDL Java/bootstrap classes under `os/android/app/src/main/java/org/libsdl/` | zlib | https://libsdl.org/ |
| This fork's own changes (Android activities, `SoundFont.java`, dual-screen/layout work, Windows build probe) | GPL-2.0-only | this repository |

## 3. Bundled data

| Component | License | Where distributed | Obligation |
|---|---|---|---|
| TimGM6mb General-MIDI soundfont | GPL-2 (© 2004 Tim Brechbill, 2010 David Bolton) | `assets/soundfont.sf2` inside the Android APK; `soundfont.sf2` beside `openttd.exe` in the Windows package | Include attribution and the GPL-2 text; upstream font source: https://github.com/musescore/musescore-old/commit/90c33ef9d87b3f5ff92efd3b07d89eb455fb1fef |

The soundfont is data used at runtime by FluidSynth, not code merged into the
executable, but it is still redistributed, so it carries its notice.

Game graphics, sounds and music (OpenGFX, OpenSFX, OpenMSX, NewGRFs, AIs, game
scripts, original TTD data) are **not** distributed here and retain their own
licenses.

## 4. Build dependencies that get linked in

Dependency set comes from `vcpkg.json`; the exact set is whatever `vcpkg list`
reports for a configured build. Licenses verified from each port's `copyright` file:

| Component | License | Linked into |
|---|---|---|
| SDL2 | zlib | Linux, Windows, Android (static) |
| FreeType | FTL or GPL-2 (dual; FTL applies to the font engine as used) | all |
| HarfBuzz | Old MIT / MIT | all |
| ICU | Unicode License V3 | all |
| curl | curl license | Linux, Windows, Android (Windows/Android via vcpkg) |
| OpenSSL | Apache-2.0 | Android only — the Windows curl port uses the system `schannel` backend instead, so no OpenSSL ships in the Windows package |
| libpng | libpng-2.0 | all |
| zlib | zlib | all |
| liblzma (xz) | mixed: most files 0BSD/public domain, some PD | all |
| lzo | GPL-2.0 | all (optional feature: pre-0.3.0 savegames) |
| soxr | LGPL-2.1 | all |
| opusfile / opus / ogg | BSD-3-Clause | all |
| nghttp2, brotli | MIT | all (curl HTTP/2 and compression) |
| bzip2 | bzip2-1.0.6 | build support |
| gcem | Apache-2.0 | build support (header-only, fluidsynth) |
| FluidSynth | LGPL-2.1 | Android (inside `libmain.so`), Windows (statically into `openttd.exe`) |

### Static-linking consequences

- **GPL-2.0 components** (`lzo`): static linking into an already-GPL-2.0 work is
  fine; the combination stays GPL-2.0. LZO is optional and can be dropped from
  `vcpkg.json` if a fully non-GPL dependency set is ever required.
- **LGPL components** (`soxr`, `fluidsynth`): statically linking an LGPL library
  requires that the recipient can relink or replace that library. For this fork that
  means publishing the exact build recipe (`vcpkg.json` pinned to a baseline +
  triplet + toolchain) and the ability to supply object files, so the library can be
  swapped without re-deriving the whole tree. Both are satisfied by building from
  this repository as documented; contact the maintainer for object archives.

## 5. Per-package status

- **Linux** (`Builds/openttd-<ver>-linux-x86_64/`): dynamically links distro
  libraries. `COPYING.md` + `LICENSE.txt` included; the GPL-2.0 text covers the
  engine, and the distro packages carry their own license texts on the system.
- **Windows** (`Builds/openttd-<ver>-win64/`): statically linked against the vcpkg
  set above (no OpenSSL, no dynamic runtime DLLs beyond the Windows system ones).
  Includes `soundfont.sf2` (GPL-2), `COPYING.md` and `LICENSE.txt`.
- **Android** (`Builds/*.apk`): self-contained APK; `libmain.so` statically links
  FluidSynth (LGPL-2.1), soxr, ICU, FreeType, HarfBuzz, SDL2, curl, OpenSSL, etc.
  The asset soundfont is GPL-2. The APK itself is not redistributable content — it
  is a derived GPL-2.0 work; corresponding source is this repository. (An APK cannot
  carry a license file in a way Android surfaces, so the notices live in the repo.)

## 6. Not compliant yet / known gaps

- Inherited upstream workflows under `.github/workflows/` include release/upload
  automation meant for the OpenTTD project's own infrastructure. They are inert on
  this fork but should be removed so nobody mistakes them for working release
  machinery.
- Packages are built and published manually; there is no automated attestation that
  each uploaded artifact matches a tagged commit.
