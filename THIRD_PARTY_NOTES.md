# Third-Party Notices

This fork contains or builds against third-party components. This file records
provenance; each component retains its own license. The distribution obligations
(what must ship with a binary, and what static linking requires) are worked out in
[`upstream-port/LICENSES.md`](upstream-port/LICENSES.md).

## Engine

| Component | License | Source |
|---|---|---|
| OpenTTD | GPL-2.0-only | https://github.com/OpenTTD/OpenTTD (base tag `16.0-RC1`, `07b339c0`) |

Engine-bundled components keep their own licenses — squirrel/md5 (Zlib), fmt
(MIT), nlohmann-json (MIT), OpenGL headers (MIT), catch2 (BSL-1.0), icu scriptrun
(Unicode), monocypher (BSD-2-Clause/CC0), social integration API (MIT),
`CheckAtomic.cmake` (Apache-2.0). See the upstream README's "Licensing" section and
the license files beside each directory.

## Code added by this fork

| Component | License | Source |
|---|---|---|
| psyycker/openttd-android Gradle/NDK/vcpkg layer | GPL-2.0-only | https://github.com/psyycker/openttd-android @ `9407a7f` |
| SDL Java/bootstrap classes under `os/android/app/src/main/java/org/libsdl/` | zlib | https://libsdl.org/ |
| This fork's changes (Android activities, `SoundFont.java`, Windows build probe) | GPL-2.0-only | this repository |

## Bundled data

| Component | License | Where |
|---|---|---|
| TimGM6mb General-MIDI soundfont | GPL-2 (© 2004 Tim Brechbill, 2010 David Bolton) | Android asset `assets/soundfont.sf2`; Windows `soundfont.sf2` |

Vendored glue is additive and isolated; see `upstream-port/` for the exact set of
files and the adaptation notes.

## Build-time dependencies (via vcpkg / system packages)

Each port carries its own license; verified from `share/<port>/copyright` in a
configured build tree. License names and the per-package consequences (including the
LGPL static-linking obligations) are listed in
[`upstream-port/LICENSES.md`](upstream-port/LICENSES.md). Note that the Windows curl
build uses the system `schannel` backend, so **no OpenSSL ships on Windows**, while the
Android APK does include Apache-2.0 OpenSSL.

## Game content

OpenTTD does not ship game graphics, sounds or music. Base sets (OpenGFX,
OpenSFX, OpenMSX) are downloaded separately and retain their own licenses.
