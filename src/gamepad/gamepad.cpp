/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with OpenTTD.
 * If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file gamepad.cpp Controller discovery, hotplug and diagnostics. */

#include "stdafx.h"

#include "gamepad.h"

#include "../debug.h"
#include "../gamepad_debug.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

/**
 * Whether raw controller events are logged. Always on while this fork is under test;
 * release builds compile the diagnostics out with \c GAMEPAD_DEBUG_OFF.
 */
static const bool _gamepad_verbose =
#ifdef GAMEPAD_DEBUG_OFF
	false;
#else
	true;
#endif

/** Joysticks we keep open; SDL only reports events for opened devices. */
static std::vector<SDL_Joystick *> _gamepads;

/**
 * Open a controller and describe it for the diagnostics window.
 * @param index The SDL joystick index.
 * @return The handle (null when it could not be opened) and the description.
 */
static std::pair<SDL_Joystick *, std::string> OpenGamepad(int index)
{
	const char *name = SDL_IsGameController(index) ? SDL_GameControllerNameForIndex(index) : SDL_JoystickNameForIndex(index);
	char guid[33];
	SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index), guid, sizeof(guid));
	int axes = 0;
	int buttons = 0;
	int hats = 0;
	SDL_Joystick *joystick = SDL_JoystickOpen(index);
	if (joystick != nullptr) {
		axes = SDL_JoystickNumAxes(joystick);
		buttons = SDL_JoystickNumButtons(joystick);
		hats = SDL_JoystickNumHats(joystick);
		if (name == nullptr) name = SDL_JoystickName(joystick);
	}
	return {joystick, fmt::format("controller {} [{}]: {} ({} axes, {} buttons, {} hats)", index, guid,
		name != nullptr ? name : "<unknown>", axes, buttons, hats)};
}

/** Handle device changes and, when enabled, raw axis/button/hat events. */
static int SDLCALL GamepadEventFilter(void *, SDL_Event *event)
{
	switch (event->type) {
		case SDL_JOYDEVICEADDED: {
			auto [joystick, description] = OpenGamepad(event->jdevice.which);
			if (joystick != nullptr) _gamepads.push_back(joystick);
			std::string line = "connected: " + description;
			GamepadDebugRecord(line);
			Debug(Facility::Driver, Severity::Critical, "Gamepad: {}", line);
			break;
		}

		case SDL_JOYDEVICEREMOVED: {
			SDL_Joystick *joystick = SDL_JoystickFromInstanceID(event->jdevice.which);
			auto it = std::ranges::find(_gamepads, joystick);
			if (it != std::end(_gamepads)) {
				_gamepads.erase(it);
				if (joystick != nullptr) SDL_JoystickClose(joystick);
			}
			GamepadDebugRecord(fmt::format("disconnected (device id {})", event->jdevice.which));
			break;
		}

		default:
			if (!_gamepad_verbose) break;
			switch (event->type) {
				case SDL_JOYAXISMOTION:
					if (std::abs(event->jaxis.value) > 1000) {
						GamepadDebugRecord(fmt::format("axis {} moved to {}", event->jaxis.axis, event->jaxis.value));
					}
					break;

				case SDL_JOYBUTTONDOWN:
					GamepadDebugRecord(fmt::format("button {} pressed", event->jbutton.button));
					break;

				case SDL_JOYBUTTONUP:
					GamepadDebugRecord(fmt::format("button {} released", event->jbutton.button));
					break;

				case SDL_JOYHATMOTION:
					GamepadDebugRecord(fmt::format("hat {} moved to {}", event->jhat.hat, event->jhat.value));
					break;

				default: break;
			}
			break;
	}
	return 0;
}

void StartGamepads()
{
	if (SDL_WasInit(SDL_INIT_JOYSTICK) == 0 && SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0) {
		Debug(Facility::Driver, Severity::Warning, "Gamepad: controller support unavailable: {}", SDL_GetError());
		return;
	}
	/* The game-controller mapping database is optional; raw joystick events suffice. */
	if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
		const char *error = SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0 ? SDL_GetError() : "";
		Debug(Facility::Driver, Severity::Critical, "Gamepad: controller mappings {}",
			error[0] != '\0' ? fmt::format("unavailable: {}", error) : "loaded");
	}

	int count = SDL_NumJoysticks();
	GamepadDebugRecord(fmt::format("connected controllers: {}", count));
	Debug(Facility::Driver, Severity::Critical, "Gamepad: {} controller(s) connected", count);
	for (int i = 0; i < count; i++) {
		auto [joystick, description] = OpenGamepad(i);
		if (joystick != nullptr) _gamepads.push_back(joystick);
		GamepadDebugRecord(description);
		Debug(Facility::Driver, Severity::Critical, "Gamepad: {}", description);
	}

	SDL_AddEventWatch(GamepadEventFilter, nullptr);
}

void StopGamepads()
{
	for (SDL_Joystick *joystick : _gamepads) {
		if (joystick != nullptr) SDL_JoystickClose(joystick);
	}
	_gamepads.clear();

	if (SDL_WasInit(SDL_INIT_JOYSTICK)) SDL_DelEventWatch(GamepadEventFilter, nullptr);
	if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
	if (SDL_WasInit(SDL_INIT_JOYSTICK)) SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

void UpdateGamepads()
{
	if (SDL_WasInit(SDL_INIT_JOYSTICK) == 0) return;
	SDL_PumpEvents();
}
