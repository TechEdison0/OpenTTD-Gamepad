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
#include "controller_config.h"

#include "../debug.h"
#include "../gamepad_debug.h"

#include <SDL.h>

#include <algorithm>
#include <array>
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

/**
 * Live state of the selected controller's raw controls. Everything else is discarded,
 * so the Controller window can show which mapped control is currently active without
 * touching the diagnostics feed (which is compiled out of release builds).
 */
static std::array<bool, 128> _gamepad_live_buttons = {};
static std::array<int16_t, 64> _gamepad_live_axes = {};
static std::array<bool, 64> _gamepad_live_axis_seen = {};
static std::array<uint8_t, 16> _gamepad_live_hats = {};
/** Guards the live arrays: SDL events write on the event thread, windows read on the game thread. */
static std::mutex _gamepad_live_mutex;

/** Forget the previous controller's control state. */
static void ClearGamepadLiveState()
{
	std::lock_guard<std::mutex> lock(_gamepad_live_mutex);
	_gamepad_live_buttons.fill(false);
	_gamepad_live_axes.fill(0);
	_gamepad_live_axis_seen.fill(false);
	_gamepad_live_hats.fill(SDL_HAT_CENTERED);
}

/** Whether an SDL joystick event came from the selected controller. */
static bool IsSelectedGamepad(SDL_JoystickID which)
{
	if (_gamepad_selection < 0 || static_cast<size_t>(_gamepad_selection) >= _gamepad_slots.size()) return false;
	return _gamepad_slots[_gamepad_selection].instance_id == which;
}

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
				ClearGamepadLiveState();
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

	/* The mapping flow needs raw events even in builds with diagnostics compiled out. */
	switch (event->type) {
		case SDL_JOYAXISMOTION:
			ControllerMappingEvent(ControllerInputEventType::AxisMotion, event->jaxis.axis, event->jaxis.value);
			break;

		case SDL_JOYBUTTONUP:
			ControllerMappingEvent(ControllerInputEventType::ButtonUp, event->jbutton.button, 0);
			break;

		case SDL_JOYHATMOTION:
			ControllerMappingEvent(ControllerInputEventType::HatMotion, event->jhat.hat, event->jhat.value);
			break;

		default: break;
	}

	/* Live control state drives the visual feedback in the Controller window. It is
	 * tracked independently of the diagnostics feed and the mapping flow. */
	switch (event->type) {
		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			if (IsSelectedGamepad(event->jbutton.which) && event->jbutton.button < _gamepad_live_buttons.size()) {
				std::lock_guard<std::mutex> lock(_gamepad_live_mutex);
				_gamepad_live_buttons[event->jbutton.button] = event->type == SDL_JOYBUTTONDOWN;
			}
			break;

		case SDL_JOYAXISMOTION: {
			const GamepadSlot *slot = FindSlot(event->jaxis.which);
			if (slot == nullptr || std::ranges::find(slot->usable_axes, event->jaxis.axis) == std::end(slot->usable_axes)) break;
			if (IsSelectedGamepad(event->jaxis.which) && event->jaxis.axis < _gamepad_live_axes.size()) {
				std::lock_guard<std::mutex> lock(_gamepad_live_mutex);
				_gamepad_live_axes[event->jaxis.axis] = event->jaxis.value;
				_gamepad_live_axis_seen[event->jaxis.axis] = true;
			}
			break;
		}

		case SDL_JOYHATMOTION:
			if (IsSelectedGamepad(event->jhat.which) && event->jhat.hat < _gamepad_live_hats.size()) {
				std::lock_guard<std::mutex> lock(_gamepad_live_mutex);
				_gamepad_live_hats[event->jhat.hat] = event->jhat.value;
			}
			break;

		default: break;
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
	ClearGamepadLiveState();

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
	ClearGamepadLiveState();
}

bool GetGamepadLiveButton(uint8_t index)
{
	std::lock_guard<std::mutex> lock(_gamepad_live_mutex);
	return index < _gamepad_live_buttons.size() && _gamepad_live_buttons[index];
}

int16_t GetGamepadLiveAxis(uint8_t index)
{
	if (index >= _gamepad_live_axes.size()) return 0;
	/* Query SDL before taking our lock: SDL queries can run the event watch, which
	 * writes the live arrays and would deadlock on a non-recursive mutex. */
	bool trigger = IsGamepadTriggerAxis(index);
	std::lock_guard<std::mutex> lock(_gamepad_live_mutex);
	/* Trigger axes rest at the bottom of their range, not at zero. Until SDL reports
	 * the first event they must still read as resting, otherwise a freshly loaded
	 * config looks permanently active and mapping needs a second click. */
	if (!_gamepad_live_axis_seen[index] && trigger) return -32768;
	return _gamepad_live_axes[index];
}

bool IsGamepadTriggerAxis(uint8_t index)
{
	if (_gamepad_selection < 0 || static_cast<size_t>(_gamepad_selection) >= _gamepad_slots.size()) return false;
	const GamepadSlot &slot = _gamepad_slots[_gamepad_selection];
	if (slot.gamecontroller == nullptr) return false;
	for (SDL_GameControllerAxis trigger : {SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT}) {
		SDL_GameControllerButtonBind bind = SDL_GameControllerGetBindForAxis(slot.gamecontroller, trigger);
		if (bind.bindType == SDL_CONTROLLER_BINDTYPE_AXIS && bind.value.axis == index) return true;
	}
	return false;
}

uint8_t GetGamepadLiveHat(uint8_t index)
{
	std::lock_guard<std::mutex> lock(_gamepad_live_mutex);
	return index < _gamepad_live_hats.size() ? _gamepad_live_hats[index] : static_cast<uint8_t>(SDL_HAT_CENTERED);
}

void UpdateGamepads()
{
	if (SDL_WasInit(SDL_INIT_JOYSTICK) == 0) return;
	SDL_PumpEvents();
}
