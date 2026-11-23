/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file gamepad_debug.h On-screen controller diagnostics (chunk C0). */

#ifndef GAMEPAD_DEBUG_H
#define GAMEPAD_DEBUG_H

#include <string_view>

/**
 * Record a line about controller state or an event, shown in the diagnostics window.
 * Safe to call from any thread.
 * @param text The line to record.
 */
void GamepadDebugRecord(std::string_view text);

/**
 * Open the controller diagnostics window. It shows the connected controllers and the
 * most recent controller events.
 */
void ShowGamepadDebugWindow();

#endif /* GAMEPAD_DEBUG_H */
