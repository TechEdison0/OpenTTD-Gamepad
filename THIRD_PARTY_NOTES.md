# Third-Party Notices

This fork contains or builds against third-party components. This file records
provenance; each component retains its own license.

## Engine

| Component | License | Source |
|---|---|---|
| OpenTTD | GPL-2.0-only | https://github.com/OpenTTD/OpenTTD (base tag `16.0-RC1`, `07b339c0`) |

## Vendored build glue

| Component | License | Source |
|---|---|---|
| psyycker/openttd-android Gradle/NDK/vcpkg layer | GPL-2.0-only | https://github.com/psyycker/openttd-android @ `9407a7f` |
| SDL Java/bootstrap classes under `os/android/app/src/main/java/org/libsdl/` | zlib | https://libsdl.org/ |

Vendored glue is additive and isolated; see `upstream-port/` for the exact set of
files and the adaptation notes.

## Build-time dependencies (via vcpkg / system packages)

Each port carries its own license. As of the initial pin this includes, among
others: SDL2, FreeType, HarfBuzz, ICU, libpng, zlib, liblzma, lzo, curl, openssl,
soxr, opusfile, breakpad, fontconfig, dbus. Run `vcpkg list` in a configured build
for the authoritative set.

## Game content

OpenTTD does not ship game graphics, sounds or music. Base sets (OpenGFX,
OpenSFX, OpenMSX) are downloaded separately and retain their own licenses.
