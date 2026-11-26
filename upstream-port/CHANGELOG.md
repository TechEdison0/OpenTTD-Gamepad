# Changelog — OpenTTD-Gamepad fork

Fork-only changelog. Upstream OpenTTD changes belong to the upstream project; this
file records only what this fork adds.

## 2026-11-26 — C1 bugfix: explicit axis calibration gesture

- Mapping now starts with only the **Press A** step visible. After A is mapped, a prompt
  asks the user to move both sticks in a circle, press and release LT/RT, release them,
  then press A again. Only after that second A press do the remaining mapping steps appear.
- The gesture records each stick/trigger axis's observed range and uses its **longest
  stable value** as rest, not the most negative sample. A pad that rests near `-128` while
  travelling to `-32767` therefore stores `-128` as rest, and a brief press cannot replace
  that baseline.
- Removed the automatic startup rest-sampling routine; controllers that rest away from the
  assumed values, such as the 8BitDo Ultimate MG at `-128`, no longer auto-map every axis.
- Gamepad-generated A clicks no longer activate the **Map Controller / Cancel** widget
  when the crosshair is parked over it, so the A that confirms calibration cannot cancel
  the flow.
- A short axis ignore window is applied after calibration confirms to discard axis events
  still in flight from the gesture.
- Axis capture is now edge-triggered: an axis must first report a value near its learned
  rest, then move beyond the detection threshold, before it can satisfy a prompt. Axes
  whose calibration gesture did not show a meaningful range are skipped.
- Controllers with an SDL game-controller axis mapping now use SDL's normalized logical
  axes directly: sticks are centered near zero and triggers run `0..32767`, so no custom
  rest gesture is required and standard pads such as SN30 Pro keep the simple flow.
- Raw/unmapped controllers still use the explicit gesture, learned rest/range and
  edge-triggered capture. The two domains are never mixed at runtime.
- `controller.cfg` axis values now include the rest value: `axis:N:min:max:rest`. Older
  four-field values still parse; their rest is inferred from the step type.

## 2026-11-26 — `v20261126-c2-action-layer` (C2 complete)

### Added

- Normalized gamepad actions with press-and-release semantics in `src/gamepad/actions.*`,
  dispatched from `InputLoop()` through the engine's existing click/hotkey paths. No
  synthetic SDL events.
- Input-source tracking: real keyboard/mouse activity yields immediately; held pad
  buttons survive crosshair movement and release on the physical button.
- A/X are held left/right clicks, Y cancels the active tool, LB/RB are held Ctrl/Shift,
  L3 pauses, R3 toggles fast forward and LT/RT zoom one step per press.
- Left-stick crosshair with a shared scaled-radial-deadzone and gamma-2 response curve
  (`ApplyGamepadStickResponse()`), reused later by C4 right-stick pan.
- Mutex protection for live pad state shared between the SDL event thread and the game
  thread.

### Fixed during manual pass

- Held state is polled from SDL so buttons cannot appear to release early on Android.
- Mouse movement no longer cancels a held gamepad click.
- LB/RB modifiers are ORed into the video driver's modifier state before its transition
  check, so bulldoze no longer toggles every frame and Ctrl is not inverted on release.
- Drag-build and context menus use the real `MouseLoop()` path on both press and release.
- SDL bind queries no longer run while holding the live-state mutex, removing an
  event-watch self-deadlock seen on Android.
- The in-game options cog has a **Controller Guide** entry below Game Options.

### Verified

Compiled and manually verified on Linux x86_64, Windows under Wine, and Android
arm64-v8a (Galaxy S25 Ultra).

## 2026-11-26 — `v20261126-c1-controller-implementation` (C1 complete)

Cut from `main`; Linux, Windows and Android artifacts built from that revision,
`-DGAMEPAD_DEBUG_OFF` everywhere. Verified by hand on all three targets.

### Added

- **Controller launcher** under the main menu with **Controller Guide** and
  **Controllers & Mapping** entries. Both new windows have dark panel backgrounds and
  the launcher is centered.
- **Controller Guide**: a plain `Button -> Description` list with indented examples,
  built-in arrow glyphs and sprite bullets. The former schematic graphic was removed.
- Inline glyph rendering for arrow control codes, so labels show real glyphs instead of
  literal `{..._ARROW}` text. D-pad and stick labels use the same renderer.
- Three-column mapping list (`Control`, `Mapping`, `Live`) with headers drawn at the
  top of each column. Normal font throughout.

### Fixed

- Back/Select/Start/Guide are no longer prompted for; the flow is 18 steps. Pause is
  reserved for L3; R3 toggles fast forward.
- Trigger axes rest at `-32768` until the first SDL event, removing the green active
  state on first launch and the need to double-click during mapping. Only axis-mapped
  triggers are affected.
- Axis capture now only identifies the axis (threshold ±22,000 from rest) and always
  stores the full `-32768..32767` range. Direction-dependent min/max calibration and
  inversion are gone.
- Mapping no longer freezes after the first axis: the post-capture pause is a
  time-based 500 ms ignore window, not a wait for another input event.
- Added the two missing English strings for up/down arrow glyphs.

### Notes

- `controller.cfg` keys are unchanged (`button:N`, `axis:N:min:max`, `hat:H:bits`).
- The hardcoded 1% runtime deadzone remains; settings/rebinding are deferred to the
  hardening chunk.

## 2026-11-25 — `v20261125-c1e-controller-feedback-test` (pre-release, live map feedback)

Cut from `main`; Linux, Windows and Android artifacts built from that revision,
`-DGAMEPAD_DEBUG_OFF` everywhere. Verified by hand on all three targets.

### Added

- **Live control values** in the Controller window's mapping list: `pressed`/`released`
  for buttons, the raw value for axes and the hat bitfield for D-pads, refreshed on a
  100 ms tick. State is tracked in `src/gamepad/` for the selected controller only and
  is cleared on selection change, removal and shutdown; it does not depend on the
  diagnostics window.
- Column header (`Control / Mapping / Live`) and green/grey highlighting of active rows.
- Column boundaries scale with the widget width (38% / 38% / 24%) and the dense rows
  use small text, so nothing is clipped on high-DPI screens.

### Fixed

- Trigger rows showed active at rest: SDL triggers rest at the bottom of their
  calibrated range, so the old center-of-range test treated the resting value as pressed.
  Triggers now activate only above `min + 25%` of range; sticks keep a fixed ~25%
  magnitude threshold.

### Verified

Live values and trigger rest state on Linux x86_64, the Windows build under Wine and
Android arm64-v8a (Galaxy S25 Ultra). Mapping and persistence from C1d unchanged.

### Note

`std::to_string` is banned by `safeguards.h`; value text uses `fmt::format`.

## 2026-11-25 — `v20261125-c1d-controller-mapping-test` (pre-release, controller mapping)

Cut from `main`; Linux, Windows and Android artifacts built from that revision,
`-DGAMEPAD_DEBUG_OFF` everywhere.

### Added

- **Mapping flow** (`src/gamepad/controller_config.*`): 21 prompt-driven capture steps
  in the Controller window — face/shoulder/system buttons, D-pad directions, stick
  axes, triggers. Buttons capture on release after a short settle; stick axes must
  move past half travel and return before they count; triggers accept a digital button
  or an analog pull; the D-pad records the hat direction when available.
- **Persistence**: captured values written to `controller.cfg` per controller GUID
  (`button:N`, `axis:N:min:max`, `hat:H:bits`), parsed back on load, and auto-saved when
  the flow finishes. The window shows the live prompt, highlights the current step and
  lists every captured value; Map Controller toggles to Cancel and selection/load/save
  are locked mid-flow.
- **Noise-axis filtering**: mapped devices only expose axes bound to the six standard
  controller axes, so gyro/accelerometer-style extras cannot be selected; sensor events
  are ignored and all sensor types explicitly disabled at open.

### Fixed

- Trigger capture: SDL triggers rest at `-32767` and close at `+32767`, so the
  baseline-relative delta reached 65534 and overflowed `int16_t` (wrapping to -2),
  which made engagement impossible. The delta is computed in `int`; stick axes were
  unaffected because their baseline is near zero.

### Verified

- Full mapping verified by hand on Linux, Windows under Wine and Android; triggers
  capture correctly on X-input pads across Bluetooth and 2.4 GHz.

## 2026-11-25 — `v20261125-c1c-controller-selection-test` (pre-release, controller selection)

Cut from `main` at **`abe0e800f8`** (commits `f7b628133d`, `abe0e800f8`); Linux, Windows
and Android artifacts built from that revision, `-DGAMEPAD_DEBUG_OFF` everywhere.

### Added

- **Device registry** (`src/gamepad/`): connected controllers are tracked with name and
  SDL GUID across hotplug, and exposed through `GetGamepadCount`/`GetGamepadInfo` plus a
  selection index.
- **Controller window**: the dropdown is populated from the registry; its caption now
  comes from `Window::GetWidgetString` — the same mechanism the options window's
  language/autosave/currency dropdowns use — so it displays the selected controller.
  Selection updates immediately; the last config action is shown under the mapping area.
- **`controller.cfg`** beside `hotkeys.cfg`: `[controller] selected=<guid>` plus a
  per-GUID section holding an (empty until C1d) mapping list. Loading a controller with
  no entry writes a fresh one.
- **Startup restore**: `LoadControllerConfig()` runs right after `StartGamepads()`, so the
  saved selection is reapplied automatically; **Load**/**Save** remain available.

### Removed

- The first-run participation-survey prompt, and survey transmission generally:
  `NetworkSurveyHandler::IsSurveyPossible()` now returns false, which compiles out every
  transmit path (including crashlog) and hides the settings/network preview entry. This
  fork does not send data to upstream endpoints.

### Verified

- Desktop Linux, the x86_64 Windows build under Wine, and the Galaxy S25 Ultra: the
  dropdown lists connected pads, the caption updates on selection, **Save** writes
  `controller.cfg`, the selection survives a restart, and no survey prompt appears on a
  clean config.

### Known limitations

- No mappings yet (empty keys), no mapping UI beyond the placeholder area.
- Base graphics/sound/music sets are not shipped.

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