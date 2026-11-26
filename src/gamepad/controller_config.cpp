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

/** @file controller_config.cpp controller.cfg: per-controller identity and mappings. */

#include "../stdafx.h"

#include "controller_config.h"
#include "gamepad.h"

#include "../ini_type.h"
#include "../window_func.h"
#include "../window_type.h"

#include <SDL.h>

#include <array>
#include <string>
#include <vector>

#include "../safeguards.h"

std::string _controller_file;

/** Last action result, surfaced in the Controller window status line. */
static std::string _controller_status;

/** Step kinds accepted by the capture flow. */
enum MappingStepFlag : uint8_t {
	MAPPING_STEP_BUTTON  = 0, ///< digital button only.
	MAPPING_STEP_AXIS    = 1, ///< analog axis only.
	MAPPING_STEP_DPAD    = 2, ///< hat direction, or a plain button on hat-less pads.
	MAPPING_STEP_TRIGGER = 4, ///< digital button or analog axis.
};

/** One capture step: config key, display label and accepted input kind. */
struct MappingStepDef {
	const char *key;
	const char *label;
	MappingStepFlag flag;
};

/** Capture order: face/shoulder/system buttons, D-pad, sticks, triggers. */
static constexpr std::array<MappingStepDef, 18> _mapping_steps = {{
	{"a",             "A",              MAPPING_STEP_BUTTON},
	{"b",             "B",              MAPPING_STEP_BUTTON},
	{"x",             "X",              MAPPING_STEP_BUTTON},
	{"y",             "Y",              MAPPING_STEP_BUTTON},
	{"leftshoulder",  "L Shoulder",       MAPPING_STEP_BUTTON},
	{"rightshoulder", "R Shoulder",       MAPPING_STEP_BUTTON},
	{"leftstick",     "L Stick",          MAPPING_STEP_BUTTON},
	{"rightstick",    "R Stick",          MAPPING_STEP_BUTTON},
	{"dpup",          "D-pad up @U",     MAPPING_STEP_DPAD},
	{"dpdown",        "D-pad down @D",   MAPPING_STEP_DPAD},
	{"dpleft",        "D-pad left @L",   MAPPING_STEP_DPAD},
	{"dpright",       "D-pad right @R",  MAPPING_STEP_DPAD},
	{"leftx",         "L Stick @L @R",   MAPPING_STEP_AXIS},
	{"lefty",         "L Stick @U @D",   MAPPING_STEP_AXIS},
	{"rightx",        "R Stick @L @R",   MAPPING_STEP_AXIS},
	{"righty",        "R Stick @U @D",   MAPPING_STEP_AXIS},
	{"lefttrigger",   "L Trigger",   MAPPING_STEP_TRIGGER},
	{"righttrigger",  "R Trigger",   MAPPING_STEP_TRIGGER},
}};

/**
 * Hardcoded in-game function of each mapping slot (C1f). The controller remapping only
 * decides which physical control feeds a slot; C2/C3 placeholders are marked as such
 * and have no runtime behavior yet.
 */
static constexpr std::array<const char *, _mapping_steps.size()> _mapping_descriptions = {{
	"Left click / hold-drag",
	"Close focused widget (C3 placeholder)",
	"Right click",
	"Esc: cancel tool / abort build",
	"Hold: Ctrl modifier",
	"Hold: Shift modifier",
	"L3: pause/resume",
	"R3: fast forward toggle",
	"Focus up (C3)",
	"Focus down (C3)",
	"Focus left (C3)",
	"Focus right (C3)",
	"Crosshair (C2)",
	"Crosshair (C2)",
	"Pan (C2 placeholder)",
	"Pan (C2 placeholder)",
	"Zoom (C2)",
	"Zoom (C2)",
}};

/** Movement needed before an axis counts as deliberate input. */
static constexpr int16_t _mapping_axis_threshold = 22000;
/** Ignore inputs shortly after a prompt appears, so held controls do not leak in. */
static constexpr uint32_t _mapping_settle_ms = 250;
/** Ignore axis inputs briefly after a capture so one gesture cannot map two axes. */
static constexpr uint32_t _mapping_axis_quiet_ms = 500;
/** Hardcoded runtime deadzone for axes (about 1% of the full range). */
static constexpr int16_t _mapping_deadzone = 327;

/** Values captured so far, indexed like #_mapping_steps. */
static std::array<MappingValue, _mapping_steps.size()> _mapping_values;

/** Mapping flow state. */
static bool _mapping_active = false;
static size_t _mapping_step = 0;
static uint32_t _mapping_step_started = 0;
/** Axis events before this tick are ignored after a capture. */
static uint32_t _mapping_axis_ignore_until = 0;

/** Human-readable name of a hat direction bitfield. */
static std::string DescribeHatDirection(uint8_t bits)
{
	std::string out;
	auto append = [&](uint8_t bit, const char *name) {
		if ((bits & bit) == 0) return;
		if (!out.empty()) out += "+";
		out += name;
	};
	append(SDL_HAT_UP, "up");
	append(SDL_HAT_RIGHT, "right");
	append(SDL_HAT_DOWN, "down");
	append(SDL_HAT_LEFT, "left");
	return out.empty() ? "?" : out;
}

/** Format a captured value for controller.cfg. Empty string means "unset". */
static std::string FormatMappingValue(const MappingValue &value)
{
	switch (value.kind) {
		case MappingKind::None: return {};
		case MappingKind::Button: return fmt::format("button:{}", value.index);
		case MappingKind::Axis: return fmt::format("axis:{}:{}:{}", value.index, value.min, value.max);
		case MappingKind::Hat: return fmt::format("hat:{}:{}", value.index, value.direction);
	}
	return {};
}

/** Tolerant integer parser (the safeguarded C string functions are off-limits here). */
static long ParseMappingNumber(const std::string &text)
{
	size_t pos = 0;
	bool negative = !text.empty() && text[0] == '-';
	if (negative) pos++;
	long value = 0;
	for (; pos < text.size(); pos++) {
		if (text[pos] < '0' || text[pos] > '9') break;
		value = value * 10 + (text[pos] - '0');
	}
	return negative ? -value : value;
}

/** Parse one controller.cfg mapping item; malformed values become unset. */
static MappingValue ParseMappingValue(const std::string &text)
{
	MappingValue value;
	if (text.empty()) return value;

	auto field = [&text](size_t n) -> std::string {
		size_t begin = 0;
		for (size_t i = 0; i < n; i++) {
			begin = text.find(':', begin);
			if (begin == std::string::npos) return {};
			begin++;
		}
		size_t end = text.find(':', begin);
		return text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
	};

	std::string kind = field(0);
	if (kind == "button" && !field(1).empty()) {
		value.kind = MappingKind::Button;
		value.index = static_cast<uint8_t>(ParseMappingNumber(field(1)));
	} else if (kind == "axis" && !field(1).empty() && !field(2).empty() && !field(3).empty()) {
		value.kind = MappingKind::Axis;
		value.index = static_cast<uint8_t>(ParseMappingNumber(field(1)));
		value.min = static_cast<int16_t>(ParseMappingNumber(field(2)));
		value.max = static_cast<int16_t>(ParseMappingNumber(field(3)));
	} else if (kind == "hat" && !field(1).empty() && !field(2).empty()) {
		value.kind = MappingKind::Hat;
		value.index = static_cast<uint8_t>(ParseMappingNumber(field(1)));
		value.direction = static_cast<uint8_t>(ParseMappingNumber(field(2)));
	}
	return value;
}

/** Description of a captured value for the window's mapping list. */
static std::string DescribeMappingValue(const MappingValue &value)
{
	switch (value.kind) {
		case MappingKind::None: return {};
		case MappingKind::Button: return fmt::format("button {}", value.index);
		case MappingKind::Axis: return fmt::format("axis {} [{}, {}]", value.index, value.min, value.max);
		case MappingKind::Hat: return fmt::format("hat {} {}", value.index, DescribeHatDirection(value.direction));
	}
	return {};
}

/** Persist the selection plus the selected controller's mapping list. */
static void WriteConfigFile(const std::string &selected_guid,
	[[maybe_unused]] const std::array<MappingValue, _mapping_steps.size()> &values)
{
	IniFile ini{};
	ini.LoadFromDisk(_controller_file, Subdirectory::None);

	IniGroup &settings = ini.GetOrCreateGroup("controller");
	settings.GetOrCreateItem("selected").SetValue(selected_guid);

	if (!selected_guid.empty()) {
		IniGroup &device = ini.GetOrCreateGroup("controller." + selected_guid);
		for (size_t i = 0; i < _mapping_steps.size(); i++) {
			device.GetOrCreateItem(_mapping_steps[i].key).SetValue(FormatMappingValue(values[i]));
		}
	}

	bool saved = ini.SaveToDisk(_controller_file);
	_controller_status = saved
		? fmt::format("saved to {}", _controller_file)
		: fmt::format("save failed: {}", _controller_file);
}

/** GUID of the currently selected controller, or empty for none. */
static std::string SelectedGuid()
{
	int index = GetGamepadSelection();
	if (index >= 0 && static_cast<size_t>(index) < GetGamepadCount()) {
		return GetGamepadInfo(static_cast<size_t>(index)).guid;
	}
	return {};
}

/** Whether a raw axis belongs to the selected controller's mappable axes. */
static bool IsSelectedControllerAxis(uint8_t axis)
{
	int index = GetGamepadSelection();
	if (index < 0) return false;
	for (size_t i = 0; i < GetGamepadAxisCount(static_cast<size_t>(index)); i++) {
		if (GetGamepadAxis(static_cast<size_t>(index), i) == axis) return true;
	}
	return false;
}

/** Reset the per-step capture scratch state. */
static void ResetMappingStepState()
{
	_mapping_step_started = SDL_GetTicks();
}

/** Store the axis just detected and move on. */
static void RecordAxisAndAdvance(uint8_t axis)
{
	/* SDL axes always span the full 16-bit range regardless of how far the user
	 * happened to move the stick during capture. */
	_mapping_values[_mapping_step] = MappingValue{MappingKind::Axis, axis, -32768, 32767, 0};
	_mapping_step++;
	ResetMappingStepState();
	_mapping_axis_ignore_until = SDL_GetTicks() + _mapping_axis_quiet_ms;
}

/** Move to the next step, finishing and saving when the last one completes. */
static void AdvanceMappingStep()
{
	if (_mapping_step >= _mapping_steps.size()) {
		_mapping_active = false;
		WriteConfigFile(SelectedGuid(), _mapping_values);
		InvalidateWindowData(WindowClass::Controller, 0);
		return;
	}
	InvalidateWindowData(WindowClass::Controller, 0);
}

int LoadControllerConfig()
{
	IniFile ini{};
	ini.LoadFromDisk(_controller_file, Subdirectory::None);

	std::string selected;
	if (const IniGroup *settings = ini.GetGroup("controller")) {
		if (const IniItem *item = settings->GetItem("selected")) {
			if (item->value.has_value()) selected = *item->value;
		}
	}

	/* Resolve the saved GUID against the currently connected controllers. */
	int index = -1;
	if (!selected.empty()) {
		for (size_t i = 0; i < GetGamepadCount(); i++) {
			if (GetGamepadInfo(i).guid == selected) {
				index = static_cast<int>(i);
				break;
			}
		}
	}
	SetGamepadSelection(index);
	_controller_status = fmt::format("loaded {} (index {})", _controller_file, index);

	/* Parse any stored mappings; missing keys simply stay unset. */
	_mapping_values.fill(MappingValue{});
	if (index >= 0) {
		std::string section = "controller." + GetGamepadInfo(static_cast<size_t>(index)).guid;
		if (const IniGroup *device = ini.GetGroup(section)) {
			for (size_t i = 0; i < _mapping_steps.size(); i++) {
				if (const IniItem *item = device->GetItem(_mapping_steps[i].key)) {
					if (item->value.has_value()) _mapping_values[i] = ParseMappingValue(*item->value);
				}
			}
		} else {
			/* No entry yet for this controller: create a fresh, empty one. */
			WriteConfigFile(GetGamepadInfo(static_cast<size_t>(index)).guid, _mapping_values);
		}
	}

	return index;
}

void SaveControllerConfig()
{
	WriteConfigFile(SelectedGuid(), _mapping_values);
}

std::string GetControllerConfigStatus()
{
	return _controller_status;
}

size_t GetControllerMappingStepCount()
{
	return _mapping_steps.size();
}

const char *GetControllerMappingStepLabel(size_t step)
{
	if (step >= _mapping_steps.size()) return "";
	return _mapping_steps[step].label;
}

const char *GetControllerMappingDescription(size_t step)
{
	if (step >= _mapping_descriptions.size()) return "";
	return _mapping_descriptions[step];
}

bool IsControllerMapping()
{
	return _mapping_active;
}

std::string GetControllerMappingPrompt()
{
	if (!_mapping_active || _mapping_step >= _mapping_steps.size()) return {};
	const MappingStepDef &step = _mapping_steps[_mapping_step];
	if (step.flag == MAPPING_STEP_AXIS) {
		return fmt::format("Move {} (one direction, then release) - step {} of {}",
			step.label, _mapping_step + 1, _mapping_steps.size());
	}
	return fmt::format("Press {} (step {} of {})", step.label, _mapping_step + 1, _mapping_steps.size());
}

std::vector<std::pair<std::string, std::string>> GetControllerMappingRows()
{
	std::vector<std::pair<std::string, std::string>> rows;
	for (size_t i = 0; i < _mapping_steps.size(); i++) {
		std::string value = DescribeMappingValue(_mapping_values[i]);
		rows.emplace_back(_mapping_steps[i].label, value.empty() ? "—" : std::move(value));
	}
	return rows;
}

bool IsControllerMappingControlActive(size_t step)
{
	if (step >= _mapping_values.size()) return false;
	const MappingValue &value = _mapping_values[step];
	switch (value.kind) {
		case MappingKind::Button:
			return GetGamepadButtonHeld(value.index);

		case MappingKind::Axis: {
			/* Sticks rest near zero, triggers at the bottom of their range, so they need
			 * different tests. Use int: a full trigger swing is 65534 and overflows
			 * int16_t. */
			if (_mapping_steps[step].flag == MAPPING_STEP_TRIGGER) {
				int range = static_cast<int>(value.max) - static_cast<int>(value.min);
				if (range <= 0) return false;
				return GetGamepadLiveAxis(value.index) > static_cast<int>(value.min) + range / 4;
			}
			int live = GetGamepadLiveAxis(value.index);
			return live > _mapping_axis_threshold / 2 || live < -_mapping_axis_threshold / 2;
		}

		case MappingKind::Hat:
			return (GetGamepadLiveHat(value.index) & value.direction) != 0;

		case MappingKind::None:
			return false;
	}
	return false;
}

int GetControllerMappingAxisValue(size_t step)
{
	if (step >= _mapping_values.size()) return 0;
	const MappingValue &value = _mapping_values[step];
	if (value.kind != MappingKind::Axis) return 0;

	int minimum = static_cast<int>(value.min);
	int maximum = static_cast<int>(value.max);
	if (maximum <= minimum) return 0;
	int half_range = (maximum - minimum) / 2;
	if (half_range <= 0) return 0;
	int centre = (minimum + maximum) / 2;

	int scaled = (GetGamepadLiveAxis(value.index) - centre) * 100 / half_range;
	if (scaled > 100) scaled = 100;
	if (scaled < -100) scaled = -100;
	return scaled;
}

std::string GetControllerMappingLiveText(size_t step)
{
	if (step >= _mapping_values.size()) return "-";
	const MappingValue &value = _mapping_values[step];
	switch (value.kind) {
		case MappingKind::Button:
			return GetGamepadLiveButton(value.index) ? "pressed" : "released";

		case MappingKind::Axis:
			return fmt::format("{}", GetGamepadLiveAxis(value.index));

		case MappingKind::Hat:
			return fmt::format("{}", GetGamepadLiveHat(value.index));

		case MappingKind::None:
			return "-";
	}
	return "-";
}

bool StartControllerMapping()
{
	if (GetGamepadSelection() < 0) {
		_controller_status = "select a controller first";
		return false;
	}
	_mapping_values.fill(MappingValue{});
	_mapping_step = 0;
	_mapping_active = true;
	_mapping_axis_ignore_until = 0;
	ResetMappingStepState();
	_controller_status = "mapping: follow the prompts";
	InvalidateWindowData(WindowClass::Controller, 0);
	return true;
}

void CancelControllerMapping()
{
	_mapping_active = false;
	_mapping_axis_ignore_until = 0;
	_controller_status = "mapping cancelled";
	InvalidateWindowData(WindowClass::Controller, 0);
}

void ControllerMappingEvent(ControllerInputEventType type, uint8_t index, int16_t value)
{
	if (!_mapping_active || _mapping_step >= _mapping_steps.size()) return;

	const MappingStepDef &step = _mapping_steps[_mapping_step];
	bool settled = SDL_GetTicks() - _mapping_step_started >= _mapping_settle_ms;

	auto record_button = [&](uint8_t button) {
		_mapping_values[_mapping_step] = MappingValue{MappingKind::Button, button, -32768, 32767, 0};
		_mapping_step++;
		ResetMappingStepState();
		AdvanceMappingStep();
	};

	switch (type) {
		case ControllerInputEventType::ButtonUp:
			if (!settled) break;
			if (step.flag == MAPPING_STEP_BUTTON || step.flag == MAPPING_STEP_DPAD ||
					step.flag == MAPPING_STEP_TRIGGER) {
				record_button(index);
			}
			break;

		case ControllerInputEventType::HatMotion:
			if (!settled || step.flag != MAPPING_STEP_DPAD) break;
			if (value != SDL_HAT_CENTERED) {
				_mapping_values[_mapping_step] = MappingValue{MappingKind::Hat, index, -32768, 32767, static_cast<uint8_t>(value)};
				_mapping_step++;
				ResetMappingStepState();
				AdvanceMappingStep();
			}
			break;

		case ControllerInputEventType::AxisMotion: {
			if (step.flag != MAPPING_STEP_AXIS && step.flag != MAPPING_STEP_TRIGGER) break;
			if (!IsSelectedControllerAxis(index)) break;

			uint32_t now = SDL_GetTicks();
			/* Time-based ignore: no dependency on further axis events, so mapping
			 * cannot freeze if the user releases a stick and waits. */
			if (now < _mapping_axis_ignore_until) break;
			if (!settled) break;
			/* Pick the first axis that moves beyond the detection threshold. Sticks
			 * rest at zero; triggers rest at -32768. */
			int rest = IsGamepadTriggerAxis(index) ? -32768 : 0;
			if (std::abs(static_cast<int>(value) - rest) <= _mapping_axis_threshold) break;
			RecordAxisAndAdvance(index);
			AdvanceMappingStep();
			break;
		}
	}
}
