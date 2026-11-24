#!/usr/bin/env bash
# Launch the Windows OpenTTD-Gamepad build under Wine, and configure Wine so that
# gamepads are exposed to DirectInput/XInput first. Safe to run repeatedly.
#
# The script lives next to openttd.exe, so double-clicking it starts the game from
# that folder. Where a file manager prefers to open .sh files in an editor, mark it
# executable ("Allow executing file as program" / chmod +x) or make a desktop entry
# whose Exec points at this file.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"

wine="${WINE:-wine}"
command -v "$wine" >/dev/null 2>&1 || { echo "error: '$wine' not found; install Wine or set WINE=..." >&2; exit 1; }

# Wine hides raw HID devices from DirectInput/XInput by default, which leaves SDL-based
# games with no controllers even though Wine's own control panel sees them.
"$wine" reg add "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\winebus" \
    /v DisableHidraw /t REG_DWORD /d 1 >/dev/null
"$wine" reg add "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\winebus" \
    /v "Enable SDL" /t REG_DWORD /d 1 >/dev/null

# If the Controller diagnostics window still reports zero controllers while
# "wine control" shows the pad, uncomment the next line to keep SDL off its HIDAPI
# backend (Wine's own HID stack is used instead).
# export SDL_JOYSTICK_HIDAPI=0

# Controllers are currently handled by the SDL backend, so the SDL video driver has to
# be selected explicitly; the native win32 driver remains the default for desktop use.
# Once input is decoupled from the video driver, this flag can be dropped.
exec "$wine" "$here/openttd.exe" -v sdl-opengl "$@"
