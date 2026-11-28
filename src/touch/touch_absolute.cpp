/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file touch_absolute.cpp Absolute tap position with a plain left click and no gestures. */

#include "../stdafx.h"
#include "../gfx_func.h"
#include "../window_func.h"
#include "../core/math_func.hpp"

#include <cmath>
#include "touch_absolute.h"

#include "../safeguards.h"

/** How far a finger may move in screen pixels before it stops being a tap. */
static constexpr float TAP_SLOP_PX = 24.0f;

/** State of the single tap candidate. */
struct TapCandidate {
	bool active = false;        ///< Whether a finger is being tracked.
	SDL_FingerID finger_id = 0; ///< The tracked finger.
	float start_x = 0;          ///< Normalised starting X position.
	float start_y = 0;          ///< Normalised starting Y position.
};

static TapCandidate _tap;

/** Drop any in-progress tap. */
void TouchAbsoluteReset()
{
	_tap = {};
}

/** Place the cursor at an absolute screen position. */
static void MoveCursorToNormalised(float x, float y)
{
	int px = Clamp(static_cast<int>(x * _screen.width), 0, _screen.width - 1);
	int py = Clamp(static_cast<int>(y * _screen.height), 0, _screen.height - 1);

	if (_cursor.fix_at) {
		_cursor.UpdateCursorPositionRelative(px - _cursor.pos.x, py - _cursor.pos.y);
	} else {
		_cursor.UpdateCursorPosition(px, py);
	}
}

/** @return Whether the tracked finger stayed within the tap slop. */
static bool WithinTapSlop(const SDL_TouchFingerEvent &ev)
{
	float dx = (ev.x - _tap.start_x) * _screen.width;
	float dy = (ev.y - _tap.start_y) * _screen.height;
	return std::hypot(dx, dy) <= TAP_SLOP_PX;
}

/**
 * Handle one SDL finger event in absolute mode.
 * Multi-finger gestures and movement are ignored; a single finger tap is a left click.
 * @param ev Finger event from the SDL event loop.
 */
void TouchAbsoluteHandleEvent(const SDL_Event &ev)
{
	const SDL_TouchFingerEvent &finger = ev.tfinger;

	switch (ev.type) {
		case SDL_FINGERDOWN:
			if (_tap.active) {
				/* A second finger cancels the gesture; no multi-finger actions here. */
				_tap = {};
				return;
			}
			_tap.active = true;
			_tap.finger_id = finger.fingerId;
			_tap.start_x = finger.x;
			_tap.start_y = finger.y;
			break;

		case SDL_FINGERMOTION:
			if (!_tap.active || finger.fingerId != _tap.finger_id) return;
			if (!WithinTapSlop(finger)) _tap = {};
			break;

		case SDL_FINGERUP:
			if (!_tap.active || finger.fingerId != _tap.finger_id) return;
			if (WithinTapSlop(finger)) {
				MoveCursorToNormalised(finger.x, finger.y);
				_left_button_down = true;
				HandleMouseEvents();
				_left_button_down = false;
				_left_button_clicked = false;
				HandleMouseEvents();
			}
			_tap = {};
			break;

		default:
			break;
	}
}
