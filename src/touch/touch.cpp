/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file touch.cpp Dispatch SDL finger events to the active touch backend. */

#include "../stdafx.h"
#include "../gfx_func.h"
#include "../gamepad/actions.h"
#include "touch.h"
#include "touch_absolute.h"

#include "../safeguards.h"

/** Active touch feature set. T0 has no GUI or persistence, so this starts Disabled. */
static TouchMode _touch_mode = TouchMode::Disabled;

/** Initialise the touch backend. */
void TouchInit()
{
	_touch_mode = TouchMode::Disabled;
	TouchAbsoluteReset();
}

/**
 * Handle an SDL finger event for the active touch mode.
 * @param ev Event from the SDL event loop.
 */
void TouchHandleEvent(const SDL_Event &ev)
{
	switch (ev.type) {
		case SDL_FINGERDOWN:
		case SDL_FINGERMOTION:
		case SDL_FINGERUP:
			break;
		default:
			return;
	}

	NotifyTouchActivity();

	switch (_touch_mode) {
		case TouchMode::Disabled:
			TouchAbsoluteHandleEvent(ev);
			break;

		case TouchMode::Trackpad:
			/* Reserved for the later gesture chunk. */
			break;
	}

	_cursor.in_window = true;
}

/** Handle timed touch work; the absolute backend is event-driven only. */
void TouchTick()
{
}

/** @return The active touch feature set. */
TouchMode TouchGetMode()
{
	return _touch_mode;
}

/** Set the active touch feature set. */
void TouchSetMode(TouchMode mode)
{
	_touch_mode = mode;
	TouchAbsoluteReset();
}
