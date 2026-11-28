/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file touch.h Source-side touch backend, following the gamepad module seam. */

#ifndef TOUCH_H
#define TOUCH_H

#include <SDL.h>

#include <cstdint>

/** The active touch feature set. */
enum class TouchMode : uint8_t {
	Absolute, ///< Absolute tap position with tap-to-click only (minimal mode).
	Trackpad, ///< Full gesture mode (trackpad-like); reserved for a later chunk.
};

void TouchInit();
void TouchHandleEvent(const SDL_Event &ev);
void TouchTick();
TouchMode TouchGetMode();
void TouchSetMode(TouchMode mode);
void TouchSaveMode();

#endif /* TOUCH_H */
