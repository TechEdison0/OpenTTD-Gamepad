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

/** @file controller_config.h Persistence for controller selection and mappings. */

#ifndef GAMEPAD_CONTROLLER_CONFIG_H
#define GAMEPAD_CONTROLLER_CONFIG_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/** Path of controller.cfg, set during filesystem setup beside hotkeys.cfg. */
extern std::string _controller_file;

/** What kind of physical input a mapping slot captured. */
enum class MappingKind : uint8_t {
	None,   ///< nothing captured yet.
	Button, ///< a digital button.
	Axis,   ///< an analog axis (with calibration range).
	Hat,    ///< a hat direction.
};

/** Kind of raw input event fed to the mapping flow. */
enum class ControllerInputEventType : uint8_t {
	ButtonUp,   ///< a button was released.
	HatMotion,  ///< a hat reported a direction.
	AxisMotion, ///< an axis moved.
};

/** One captured control: which physical input it is, plus its calibration. */
struct MappingValue {
	MappingKind kind = MappingKind::None;
	uint8_t index = 0;      ///< raw SDL button/axis/hat number
	int16_t min = -32768;   ///< lowest value seen while calibrating an axis
	int16_t max = 32767;    ///< highest value seen while calibrating an axis
	uint8_t direction = 0;  ///< hat direction bit, when #MappingKind::Hat

	/** Whether anything has been captured. */
	bool IsSet() const { return this->kind != MappingKind::None; }
};

/**
 * Load the persisted controller selection from controller.cfg.
 *
 * Resolves the saved GUID against the connected controllers. When the selected
 * controller has no mapping entry yet, a fresh (empty) one is written immediately,
 * so a controller always has a section to fill in later. Any stored mappings are
 * parsed into the current mapping list.
 *
 * @return The resolved selection index, or -1 when nothing valid is selected.
 */
int LoadControllerConfig();

/**
 * Persist the current selection and the selected controller's mapping list.
 */
void SaveControllerConfig();

/** Result of the last load/save, shown in the window. */
std::string GetControllerConfigStatus();

/** Number of capture steps in the mapping flow. */
size_t GetControllerMappingStepCount();

/** Label of one capture step ("A", "Left stick X", ...). */
const char *GetControllerMappingStepLabel(size_t step);

/** Whether the mapping flow is currently capturing input. */
bool IsControllerMapping();

/**
 * Instruction for the current step, e.g. "Press A" / "Move Left stick X".
 * Empty when no mapping is in progress.
 */
std::string GetControllerMappingPrompt();

/**
 * Rows for the window's mapping area: (step label, captured value description).
 * Uncaptured steps show an em dash.
 */
std::vector<std::pair<std::string, std::string>> GetControllerMappingRows();

/**
 * Whether a mapped control is currently active on the selected controller.
 * Buttons use their pressed state, axes must move away from rest (sticks: zero;
 * triggers: the bottom of their calibrated range), hats test direction bits.
 * @param step Position in the mapping list.
 */
bool IsControllerMappingControlActive(size_t step);

/**
 * Live column text: the current value for every mapped control: \c "pressed"/"released"
 * for buttons, the raw number for axes and the hat bitfield for directions; \c "-" when
 * nothing is captured.
 */
std::string GetControllerMappingLiveText(size_t step);

/** In-game function assigned to a mapping step; hardcoded for C1f. */
const char *GetControllerMappingDescription(size_t step);

/**
 * Start capturing a mapping for the selected controller.
 *
 * @return Whether the flow started (a controller must be selected).
 */
bool StartControllerMapping();

/** Abort the mapping flow, keeping whatever was already captured. */
void CancelControllerMapping();

/**
 * Feed a raw input event to the mapping flow.
 *
 * Called from the gamepad event filter; ignored unless a mapping is running.
 * @param type Event kind.
 * @param index Raw button/axis/hat number.
 * @param value Axis or hat value; unused for button events.
 */
void ControllerMappingEvent(ControllerInputEventType type, uint8_t index, int16_t value);

#endif /* GAMEPAD_CONTROLLER_CONFIG_H */
