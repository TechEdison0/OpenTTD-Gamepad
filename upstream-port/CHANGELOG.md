# Changelog — OpenTTD-Gamepad fork

Fork-only changelog. Upstream OpenTTD changes belong to the upstream project; this
file records only what this fork adds.

## 2026-11-25 — `v20261125-c1b-controller-gui-test` (pre-release, controller GUI)

Cut from `main` at **`dda3695419`** (commits `1336eae5ad`, `dda3695419`); Linux, Windows
and Android artifacts were built from that same revision, with
`-DGAMEPAD_DEBUG_OFF` on all three targets.

### Added

- **Controller settings window** (`src/controller_gui.{h,cpp}`), a standalone window
  opened from a new **Controller** entry on the main menu directly below **Options**
  (reusing the cog sprite). It carries the selected-controller label, the controller
  dropdown (native list, placeholder until the device registry lands), the
  **Map Controller / Load / Save** buttons and a plaintext mapping area. No mapping or
  persistence logic yet — that is C1c/C1d.
- The window has its own GUI files rather than an edit to `options_gui.cpp`, keeping the
  fork diff small and rebase-friendly.
- Opaque `WWT_PANEL` background: the earlier per-widget fill left uncovered screen area
  behind the window and smeared whatever was drawn before.

### Changed

- The diagnostics window and raw-event logging are compiled out of these builds via
  `-DGAMEPAD_DEBUG_OFF`; the C0-era debug screen is absent everywhere, including the
  Android auto-open path.
- Android: `useLegacyPackaging = false` (candidate fix for the Waydroid install failure
  on the duo; see `research/TOUCH-PLAN.md`/skill notes).

### Verified

- Desktop Linux, the x86_64 Windows build under Wine, and the Galaxy S25 Ultra: menu
  entry opens the window; dropdown shows its placeholder; no diagnostics window
  anywhere.

### Known limitations

- Device selection does nothing beyond the placeholder entry; `controller.cfg` is not
  written yet; no mappings.
- Base graphics/sound/music sets are not shipped.

## 2026-11-24 — `v20261124-c0-gamepad-test` (pre-release, controller event verification)

Cut from `main` at **`637364b8ad`**; the Linux, Windows and Android artifacts in this
release were built from that same commit, and `CI - Desktop` / `CI - Android` pass on
it.

### Added

- **Controller diagnostics window** — a native OpenTTD window listing connected
  controllers (name, SDL GUID, axes/buttons/hats) and streaming connect/disconnect,
  axis, button and hat events. It opens automatically in test builds and is compiled
  out of release builds with `-DGAMEPAD_DEBUG_OFF`.
- **SDL2 linkage on Windows.** Upstream never probes SDL2 in its WIN32 branch, so no
  controller code could exist in the Windows binary at all (`-v sdl` failed with "no such
  video driver"). SDL2 is now discovered (vcpkg config package, aliased to `SDL2::SDL2`)
  and linked statically, with `SDL_MAIN_HANDLED` so OpenTTD keeps its own entry point.
  Both the SDL and native win32 video drivers remain available; autoprobe keeps win32 as
  the default.
- **Controller input moved out of the video driver** into `src/gamepad/` — its own source,
  lifecycle and per-tick SDL pump. Discovery, hotplug and diagnostics no longer depend on
  the SDL video driver, so a normal start (double-click, or `wine openttd.exe`) picks up
  controllers while the native win32 video driver keeps handling the display.
- **`upstream-port/wine_launch.sh`** — double-click launcher shipped in the Windows
  package. Applies the Wine `winebus` registry flags (`DisableHidraw`, `Enable SDL`)
  non-interactively and starts the game normally (no extra video-driver flag).
- Console logging of probe state (mapping database, device count, per-device lines) so
  Wine can be diagnosed from a terminal.

### Expected behaviour

- The window opens when the game starts; moving sticks and pressing buttons adds lines
  in real time.
- Unplugging/reconnecting a controller is logged immediately.
- Windows works with a plain start (`wine openttd.exe`, or a double-click); the launcher
  script only applies Wine's gamepad registry flags.

### Verified platforms

- Desktop Linux (native).
- Windows x86_64 build under Wine 9.0 with the winebus flags applied.
- Samsung Galaxy S25 Ultra (Android arm64-v8a).
- Waydroid: the app runs, but Bluetooth gamepads are not forwarded; layout tests only.

### Known limitations

- No bindings or mappings yet, and no D-pad navigation; the window is scaffolding for
  that work.
- Base graphics, sound and music sets are not shipped.
