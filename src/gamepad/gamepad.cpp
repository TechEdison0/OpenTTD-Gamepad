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

#include "../stdafx.h"

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

/** One opened controller plus its stable identity. */
struct GamepadSlot {
	SDL_Joystick *joystick = nullptr;
	SDL_GameController *gamecontroller = nullptr;
	SDL_JoystickID instance_id = -1;
	std::string name;
	std::string guid;
	/**
	 * Raw SDL axis indices that may be mapped to actions. For devices with a
	 * controller mapping this holds only the axes bound to the standard
	 * controller axes, so gyro/accelerometer-style extras cannot leak in; other
	 * devices list every raw axis.
	 */
	std::vector<uint8_t> usable_axes;
};

/** Joysticks we keep open; SDL only reports events for opened devices. */
static std::vector<GamepadSlot> _gamepad_slots;

/** Controller selected in the Controller window; -1 means none. */
static int _gamepad_selection = -1;

/** Close whichever handle owns the joystick. */
static void CloseGamepad(GamepadSlot &slot)
{
	if (slot.gamecontroller != nullptr) {
		/* Closing the controller also closes the joystick it owns. */
		SDL_GameControllerClose(slot.gamecontroller);
	} else if (slot.joystick != nullptr) {
		SDL_JoystickClose(slot.joystick);
	}
	slot.joystick = nullptr;
	slot.gamecontroller = nullptr;
}

/** Find a slot by SDL joystick instance id. */
static GamepadSlot *FindSlot(SDL_JoystickID instance_id)
{
	auto it = std::ranges::find_if(_gamepad_slots, [&](const GamepadSlot &slot) { return slot.instance_id == instance_id; });
	return it != std::end(_gamepad_slots) ? &*it : nullptr;
}

/**
 * Gyroscopes and accelerometers are never used for input in this fork, and they
 * emit a constant stream of events. Explicitly turn every sensor type off even
 * though nothing here ever enables one; some backends do.
 */
static void DisableUnwantedSensors(SDL_GameController *gamecontroller)
{
	static constexpr SDL_SensorType sensors[] = {
		SDL_SENSOR_ACCEL,
		SDL_SENSOR_GYRO,
		SDL_SENSOR_ACCEL_L,
		SDL_SENSOR_GYRO_L,
		SDL_SENSOR_ACCEL_R,
		SDL_SENSOR_GYRO_R,
	};
	for (SDL_SensorType type : sensors) {
		if (SDL_GameControllerHasSensor(gamecontroller, type)) {
			SDL_GameControllerSetSensorEnabled(gamecontroller, type, SDL_FALSE);
		}
	}
}

/**
 * Record which raw axes the controller mapping binds to the six standard
 * controller axes; anything else (gyro/accelerometer extras included) is ignored.
 */
static void CollectUsableAxes(GamepadSlot &slot)
{
	static constexpr SDL_GameControllerAxis axes[] = {
		SDL_CONTROLLER_AXIS_LEFTX,
		SDL_CONTROLLER_AXIS_LEFTY,
		SDL_CONTROLLER_AXIS_RIGHTX,
		SDL_CONTROLLER_AXIS_RIGHTY,
		SDL_CONTROLLER_AXIS_TRIGGERLEFT,
		SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
	};
	for (SDL_GameControllerAxis axis : axes) {
		SDL_GameControllerButtonBind bind = SDL_GameControllerGetBindForAxis(slot.gamecontroller, axis);
		if (bind.bindType == SDL_CONTROLLER_BINDTYPE_AXIS) {
			uint8_t raw = static_cast<uint8_t>(bind.value.axis);
			if (std::ranges::find(slot.usable_axes, raw) == std::end(slot.usable_axes)) {
				slot.usable_axes.push_back(raw);
			}
		}
	}
}

/** Format the diagnostics line describing a controller. */
static std::string DescribeGamepad(const GamepadSlot &slot, size_t mappable_axes, int buttons, int hats)
{
	return fmt::format("controller [{}]: {} ({} mappable axes, {} buttons, {} hats)", slot.guid,
		!slot.name.empty() ? slot.name : "<unknown>", mappable_axes, buttons, hats);
}

/**
 * Open a controller and describe it for the diagnostics window.
 * @param index The SDL joystick index.
 * @return The slot (with a null handle when it could not be opened).
 */
static GamepadSlot OpenGamepad(int index)
{
	GamepadSlot slot;
	const char *name = SDL_IsGameController(index) ? SDL_GameControllerNameForIndex(index) : SDL_JoystickNameForIndex(index);
	char guid[33];
	SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index), guid, sizeof(guid));
	slot.guid = guid;

	if (SDL_IsGameController(index)) {
		slot.gamecontroller = SDL_GameControllerOpen(index);
		if (slot.gamecontroller != nullptr) {
			slot.joystick = SDL_GameControllerGetJoystick(slot.gamecontroller);
			DisableUnwantedSensors(slot.gamecontroller);
			CollectUsableAxes(slot);
		}
	}
	if (slot.joystick == nullptr) {
		slot.joystick = SDL_JoystickOpen(index);
		if (slot.joystick != nullptr && slot.gamecontroller == nullptr) {
			/* No mapping: no way to tell sensor axes apart, so expose them all. */
			int axes = SDL_JoystickNumAxes(slot.joystick);
			for (int i = 0; i < axes; i++) slot.usable_axes.push_back(static_cast<uint8_t>(i));
		}
	}
	if (slot.joystick != nullptr) {
		slot.instance_id = SDL_JoystickInstanceID(slot.joystick);
		if (name == nullptr) name = SDL_JoystickName(slot.joystick);
	}
	slot.name = name != nullptr ? name : "";
	return slot;
}

/** Handle device changes and, when enabled, raw axis/button/hat events. */
static int SDLCALL GamepadEventFilter(void *, SDL_Event *event)
{
	switch (event->type) {
		case SDL_JOYDEVICEADDED: {
			GamepadSlot slot = OpenGamepad(event->jdevice.which);
			int buttons = slot.joystick != nullptr ? SDL_JoystickNumButtons(slot.joystick) : 0;
			int hats = slot.joystick != nullptr ? SDL_JoystickNumHats(slot.joystick) : 0;
			std::string line = "connected: " + DescribeGamepad(slot, slot.usable_axes.size(), buttons, hats);
			if (slot.joystick != nullptr) _gamepad_slots.push_back(std::move(slot));
			GamepadDebugRecord(line);
			Debug(Facility::Driver, Severity::Critical, "Gamepad: {}", line);
			break;
		}

		case SDL_JOYDEVICEREMOVED: {
			auto it = std::ranges::find_if(_gamepad_slots, [&](const GamepadSlot &slot) { return slot.instance_id == event->jdevice.which; });
			if (it != std::end(_gamepad_slots)) {
				CloseGamepad(*it);
				_gamepad_slots.erase(it);
			}
			if (_gamepad_selection >= static_cast<int>(_gamepad_slots.size())) _gamepad_selection = -1;
			GamepadDebugRecord(fmt::format("disconnected (device id {})", event->jdevice.which));
			break;
		}

		case SDL_CONTROLLERSENSORUPDATE:
		case SDL_SENSORUPDATE:
			/* Gyro/accelerometer data is never used; nothing enables sensors. */
			break;

		default:
			if (!_gamepad_verbose) break;
			switch (event->type) {
				case SDL_JOYAXISMOTION: {
					const GamepadSlot *slot = FindSlot(event->jaxis.which);
					/* Skip raw axes that are not mappable (sensor noise and friends). */
					if (slot == nullptr || std::ranges::find(slot->usable_axes, event->jaxis.axis) == std::end(slot->usable_axes)) break;
					if (std::abs(event->jaxis.value) > 1000) {
						GamepadDebugRecord(fmt::format("axis {} moved to {}", event->jaxis.axis, event->jaxis.value));
					}
					break;
				}

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
		GamepadSlot slot = OpenGamepad(i);
		int buttons = slot.joystick != nullptr ? SDL_JoystickNumButtons(slot.joystick) : 0;
		int hats = slot.joystick != nullptr ? SDL_JoystickNumHats(slot.joystick) : 0;
		std::string description = DescribeGamepad(slot, slot.usable_axes.size(), buttons, hats);
		if (slot.joystick != nullptr) _gamepad_slots.push_back(std::move(slot));
		GamepadDebugRecord(description);
		Debug(Facility::Driver, Severity::Critical, "Gamepad: {}", description);
	}

	SDL_AddEventWatch(GamepadEventFilter, nullptr);
}

void StopGamepads()
{
	for (GamepadSlot &slot : _gamepad_slots) {
		CloseGamepad(slot);
	}
	_gamepad_slots.clear();

	if (SDL_WasInit(SDL_INIT_JOYSTICK)) SDL_DelEventWatch(GamepadEventFilter, nullptr);
	if (SDL_WasInit(SDL_INIT_GAMECONTROLLER)) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
	if (SDL_WasInit(SDL_INIT_JOYSTICK)) SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

size_t GetGamepadCount()
{
	return _gamepad_slots.size();
}

GamepadInfo GetGamepadInfo(size_t index)
{
	if (index >= _gamepad_slots.size()) return {};
	const GamepadSlot &slot = _gamepad_slots[index];
	return {slot.name, slot.guid};
}

size_t GetGamepadAxisCount(size_t index)
{
	if (index >= _gamepad_slots.size()) return 0;
	return _gamepad_slots[index].usable_axes.size();
}

int GetGamepadAxis(size_t index, size_t axis)
{
	if (index >= _gamepad_slots.size()) return -1;
	const std::vector<uint8_t> &axes = _gamepad_slots[index].usable_axes;
	if (axis >= axes.size()) return -1;
	return axes[axis];
}

int GetGamepadSelection()
{
	return _gamepad_selection;
}

void SetGamepadSelection(int index)
{
	if (index < -1 || index >= static_cast<int>(_gamepad_slots.size())) return;
	_gamepad_selection = index;
}

void UpdateGamepads()
{
	if (SDL_WasInit(SDL_INIT_JOYSTICK) == 0) return;
	SDL_PumpEvents();
}
