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

/** @file gamepad.h Controller input, independent of the video driver. */

#ifndef GAMEPAD_GAMEPAD_H
#define GAMEPAD_GAMEPAD_H

/** Initialize controller support: enumerate devices and start tracking hotplug. */
void StartGamepads();

/** Release controller resources. */
void StopGamepads();

/**
 * Pump the SDL event queue. Called once per tick so controller events arrive no matter
 * which video driver is active (the native win32 driver never pumps SDL itself).
 */
void UpdateGamepads();

/** A discovered controller's stable identity. */
struct GamepadInfo {
	std::string name; ///< Display name reported by SDL.
	std::string guid; ///< SDL GUID string; used as the key in controller.cfg.
};

/** Number of currently connected controllers. */
size_t GetGamepadCount();

/**
 * Describe a connected controller.
 * @param index Position in the discovery list.
 * @return The controller's identity, or empty strings when the index is out of range.
 */
GamepadInfo GetGamepadInfo(size_t index);

/**
 * Number of mappable axes of a controller.
 *
 * Sensor-driven and otherwise unmapped raw axes (gyro/accelerometer noise) are
 * excluded; a device without a controller mapping reports all of its raw axes.
 * @param index Position in the discovery list.
 */
size_t GetGamepadAxisCount(size_t index);

/**
 * Raw SDL axis index of a controller's mappable axis, or \c -1 when out of range.
 * @param index Position in the discovery list.
 * @param axis Logical axis position as returned by #GetGamepadAxisCount().
 */
int GetGamepadAxis(size_t index, size_t axis);

/** Index of the controller selected in the Controller window, or -1 for none. */
int GetGamepadSelection();

/**
 * Select a controller from the discovery list.
 * @param index Position in the list, or -1 for none.
 */
void SetGamepadSelection(int index);

/** Whether a raw button of the selected controller is currently held. */
bool GetGamepadLiveButton(uint8_t index);

/** Current value of a raw axis of the selected controller (0 when unavailable). */
int16_t GetGamepadLiveAxis(uint8_t index);

/** Current hat bitfield of the selected controller (centered when unavailable). */
uint8_t GetGamepadLiveHat(uint8_t index);

#endif /* GAMEPAD_GAMEPAD_H */
