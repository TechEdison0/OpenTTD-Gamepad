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
#include "../gamepad/controller_config.h"  // _controller_file
#include "../gamepad/gamepad.h"            // GetGamepadSelection
#include "touch.h"
#include "touch_absolute.h"
#include "../ini_file.h"

#include "../safeguards.h"

/** Active touch feature set. Default set by TouchInit from controller.cfg or controller profile. */
static TouchMode _touch_mode = TouchMode::Absolute;

/** Initialise the touch backend: load persisted mode or default based on controller profile. */
void TouchInit()
{
	IniFile ini{};
	ini.LoadFromDisk(_controller_file, Subdirectory::None);

	if (const IniGroup *settings = ini.GetGroup("controller")) {
		if (const IniItem *item = settings->GetItem("touch_mode")) {
			if (item->value.has_value()) {
				int val = std::stoi(*item->value);
				if (val >= 0 && val <= static_cast<int>(TouchMode::Trackpad)) {
					_touch_mode = static_cast<TouchMode>(val);
				}
			}
		}
	}

	// If not set from config, default based on whether a controller profile is loaded.
	if (_touch_mode == TouchMode::Absolute) { // default static value
		int sel = GetGamepadSelection();
		if (sel < 0) {
			_touch_mode = TouchMode::Trackpad;
		}
	}

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
		case TouchMode::Absolute:
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

/** Persist the current touch mode to controller.cfg. */
void TouchSaveMode()
{
	IniFile ini{};
	ini.LoadFromDisk(_controller_file, Subdirectory::None);
	IniGroup &settings = ini.GetOrCreateGroup("controller");
	settings.GetOrCreateItem("touch_mode").SetValue(fmt::format("{}", static_cast<int>(_touch_mode)));
	ini.SaveToDisk(_controller_file);
}
