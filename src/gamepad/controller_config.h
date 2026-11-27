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
	Axis,        ///< a raw analog axis (with calibrated range).
	LogicalAxis, ///< an SDL game-controller axis (normalized range).
	Hat,         ///< a hat direction.
};

/** Kind of raw input event fed to the mapping flow. */
enum class ControllerInputEventType : uint8_t {
	ButtonUp,   ///< a button was released.
	HatMotion,  ///< a hat reported a direction.
	AxisMotion, ///< an axis moved.
};

	/** Speed preset for one physical stick. */
enum class ControllerAxisSpeed : uint8_t {
	Slow,   ///< Half the default rate.
	Normal, ///< Current C4 rate.
	Fast,   ///< Double the default rate.
};

/** Configurable axis groups. */
enum class ControllerAxisGroup : uint8_t {
	LeftStick,   ///< Left stick X/Y pair.
	RightStick,  ///< Right stick X/Y pair.
	LeftTrigger, ///< Left trigger.
	RightTrigger,///< Right trigger.
};

/** Per-controller options persisted beside the mapping list. */
struct ControllerOptions {
	bool invert_left_x = false;       ///< Invert left stick X.
	bool invert_left_y = false;       ///< Invert left stick Y.
	bool invert_right_x = false;      ///< Invert right stick X.
	bool invert_right_y = false;      ///< Invert right stick Y.
	bool invert_left_trigger = false; ///< Invert left trigger actuation.
	bool invert_right_trigger = false;///< Invert right trigger actuation.
	uint8_t deadzone_left_stick = 15;   ///< Left stick deadzone in percent.
	uint8_t deadzone_right_stick = 15;  ///< Right stick deadzone in percent.
	uint8_t deadzone_left_trigger = 15; ///< Left trigger deadzone in percent.
	uint8_t deadzone_right_trigger = 15;///< Right trigger deadzone in percent.
	bool swap_sticks = false;              ///< Swap stick axes and stick clicks.
	ControllerAxisSpeed left_stick_speed = ControllerAxisSpeed::Normal;  ///< Left stick rate preset.
	ControllerAxisSpeed right_stick_speed = ControllerAxisSpeed::Normal; ///< Right stick rate preset.
};

/** Options of the currently selected controller. */
ControllerOptions &GetControllerOptions();

/** Reload options for the current selection from controller.cfg. */
void LoadControllerOptions();

/** Whether a mapping step is inverted by the current controller options. */
bool GetControllerAxisInvert(size_t step);

/** Deadzone percentage for one configurable axis group. */
uint8_t GetControllerDeadzone(ControllerAxisGroup group);

/** Deadzone percentage for a mapping step. */
uint8_t GetControllerDeadzoneForStep(size_t step);

/** Speed preset for one physical stick. */
ControllerAxisSpeed GetControllerStickSpeed(ControllerAxisGroup group);

/** Integer speed multiplier percentage: 50, 100 or 200. */
int GetControllerSpeedMultiplier(ControllerAxisSpeed speed);

/** Whether stick axes and stick clicks are swapped. */
bool GetControllerStickSwap();

/** One captured control: which physical input it is, plus its calibration. */
struct MappingValue {
	MappingKind kind = MappingKind::None;
	uint8_t index = 0;      ///< raw SDL button/axis/hat number
	int16_t min = -32768;   ///< lowest value seen while calibrating an axis
	int16_t max = 32767;    ///< highest value seen while calibrating an axis
	int16_t rest = 0;       ///< calibrated resting value of an axis
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

/** Whether the flow is waiting for the A-confirmed calibration gesture. */
bool IsControllerMappingCalibrating();

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
 * triggers: -32768), hats test direction bits.
 * @param step Position in the mapping list.
 */
bool IsControllerMappingControlActive(size_t step);

/**
 * Normalized -100..100 value for a mapped axis step, or 0 when the step is not an axis.
 * @param step Position in the mapping list.
 */
int GetControllerMappingAxisValue(size_t step);

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
