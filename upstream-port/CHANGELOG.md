# Changelog — OpenTTD-Gamepad fork

Fork-only changelog. Upstream OpenTTD changes belong to the upstream project; this
file records only what this fork adds.

## 2026-11-24 — `v20261124-c0-gamepad-test` (pre-release, controller event verification)

Cut from `main` at the commit named by the release tag; the binaries are built from
that same commit.

### Added

- **Controller diagnostics window** — a native OpenTTD window listing connected
  controllers (name, SDL GUID, axes/buttons/hats) and streaming connect/disconnect,
  axis, button and hat events. It opens automatically in test builds and is compiled
  out of release builds with `-DGAMEPAD_DEBUG_OFF`.
- **SDL2 linkage on Windows.** Upstream never probes SDL2 in its WIN32 branch, so the
  controller code was not compiled into the Windows binary at all (`-v sdl` failed with
  "no such video driver"). SDL2 is now discovered (vcpkg config package, aliased to
  `SDL2::SDL2`) and linked statically, with `SDL_MAIN_HANDLED` so OpenTTD keeps its own
  entry point. The executable contains both the SDL and native win32 video drivers;
  driver autoprobe keeps win32 as the default.
- **`upstream-port/wine_launch.sh`** — double-click launcher shipped in the Windows
  package. Applies the Wine `winebus` registry flags (`DisableHidraw`, `Enable SDL`)
  non-interactively and starts the game with the SDL backend.
- Console logging of probe state (mapping database, device count, per-device lines) so
  Wine can be diagnosed from a terminal.

### Expected behaviour

- The window opens when the game starts; moving sticks and pressing buttons adds lines
  in real time.
- Unplugging/reconnecting a controller is logged immediately.
- On Windows use `wine_launch.sh` (or `wine openttd.exe -v sdl-opengl`). A plain
  `wine openttd.exe` still selects the native video driver and therefore shows no
  controller input until input handling is decoupled from the video driver.

### Verified platforms

- Desktop Linux (native).
- Windows x86_64 build under Wine 9.0 with the winebus flags applied.
- Samsung Galaxy S25 Ultra (Android arm64-v8a).
- Waydroid: the app runs, but Bluetooth gamepads are not forwarded; layout tests only.

### Known limitations

- No bindings or mappings yet, and no D-pad navigation; the window is scaffolding for
  that work.
- Base graphics, sound and music sets are not shipped.
