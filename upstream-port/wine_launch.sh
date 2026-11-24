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
# games with no controllers even though Wine's own control panel sees them. /f writes
# the value without prompting, and stdin is /dev/null so Wine can never spin on an
# unanswered "overwrite?" question; timeout keeps a broken Wine from hanging the
# launcher forever.
set_winebus_value() {
    timeout 60 "$wine" reg add "HKLM\System\CurrentControlSet\Services\winebus" \
        /v "$1" /t REG_DWORD /d "$2" /f </dev/null >/dev/null ||
        echo "warning: could not set winebus '$1'; continuing anyway" >&2
}

set_winebus_value DisableHidraw 1
set_winebus_value "Enable SDL" 1

# If the Controller diagnostics window still reports zero controllers while
# "wine control" shows the pad, uncomment the next line to keep SDL off its HIDAPI
# backend (Wine's own HID stack is used instead).
# export SDL_JOYSTICK_HIDAPI=0

exec "$wine" "$here/openttd.exe" "$@"
