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
static constexpr std::array<MappingStepDef, 21> _mapping_steps = {{
	{"a",             "A",              MAPPING_STEP_BUTTON},
	{"b",             "B",              MAPPING_STEP_BUTTON},
	{"x",             "X",              MAPPING_STEP_BUTTON},
	{"y",             "Y",              MAPPING_STEP_BUTTON},
	{"leftshoulder",  "Left shoulder",  MAPPING_STEP_BUTTON},
	{"rightshoulder", "Right shoulder", MAPPING_STEP_BUTTON},
	{"back",          "Back",           MAPPING_STEP_BUTTON},
	{"start",         "Start",          MAPPING_STEP_BUTTON},
	{"guide",         "Guide",          MAPPING_STEP_BUTTON},
	{"leftstick",     "Left stick",     MAPPING_STEP_BUTTON},
	{"rightstick",    "Right stick",    MAPPING_STEP_BUTTON},
	{"dpup",          "D-pad up",       MAPPING_STEP_DPAD},
	{"dpdown",        "D-pad down",     MAPPING_STEP_DPAD},
	{"dpleft",        "D-pad left",     MAPPING_STEP_DPAD},
	{"dpright",       "D-pad right",    MAPPING_STEP_DPAD},
	{"leftx",         "Left stick X (Horizontal)",   MAPPING_STEP_AXIS},
	{"lefty",         "Left stick Y (Vertical)",     MAPPING_STEP_AXIS},
	{"rightx",        "Right stick X (Horizontal)",  MAPPING_STEP_AXIS},
	{"righty",        "Right stick Y (Vertical)",    MAPPING_STEP_AXIS},
	{"lefttrigger",   "Left trigger",   MAPPING_STEP_TRIGGER},
	{"righttrigger",  "Right trigger",  MAPPING_STEP_TRIGGER},
}};

/** Movement needed before an axis counts as deliberate input. */
static constexpr int16_t _mapping_axis_threshold = 16384;
/** Ignore inputs shortly after a prompt appears, so held controls do not leak in. */
static constexpr uint32_t _mapping_settle_ms = 250;
/** Hardcoded runtime deadzone for axes (about 1% of the full range). */
static constexpr int16_t _mapping_deadzone = 327;

/** Values captured so far, indexed like #_mapping_steps. */
static std::array<MappingValue, _mapping_steps.size()> _mapping_values;

/** Mapping flow state. */
static bool _mapping_active = false;
static size_t _mapping_step = 0;
static uint32_t _mapping_step_started = 0;
static int16_t _mapping_axis_min = -32768;
static int16_t _mapping_axis_max = 32767;
static uint8_t _mapping_axis_candidate = 0;
static bool _mapping_axis_engaged = false;
static std::array<int16_t, 16> _mapping_axis_baselines = {};
static std::array<bool, 16> _mapping_axis_baseline_set = {};

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
	_mapping_axis_min = -32768;
	_mapping_axis_max = 32767;
	_mapping_axis_candidate = 0;
	_mapping_axis_engaged = false;
	_mapping_axis_baselines.fill(0);
	_mapping_axis_baseline_set.fill(false);
}

/** Store the axis tracked during this step and move on. */
static void RecordAxisAndAdvance()
{
	/* Treat values within the hardcoded deadzone as centered. */
	auto clamp_dead = [](int16_t v) { return std::abs(v) <= _mapping_deadzone ? static_cast<int16_t>(0) : v; };
	_mapping_values[_mapping_step] = MappingValue{MappingKind::Axis, _mapping_axis_candidate,
		clamp_dead(_mapping_axis_min), clamp_dead(_mapping_axis_max), 0};
	_mapping_step++;
	ResetMappingStepState();
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

bool StartControllerMapping()
{
	if (GetGamepadSelection() < 0) {
		_controller_status = "select a controller first";
		return false;
	}
	_mapping_values.fill(MappingValue{});
	_mapping_step = 0;
	_mapping_active = true;
	ResetMappingStepState();
	_controller_status = "mapping: follow the prompts";
	InvalidateWindowData(WindowClass::Controller, 0);
	return true;
}

void CancelControllerMapping()
{
	_mapping_active = false;
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
					(step.flag == MAPPING_STEP_TRIGGER && !_mapping_axis_engaged)) {
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

			if (value < _mapping_axis_min) _mapping_axis_min = value;
			if (value > _mapping_axis_max) _mapping_axis_max = value;

			/* Triggers rest at the axis minimum on some pads, so movement is
			 * measured relative to each axis' resting value. */
			int16_t baseline = 0;
			if (index < _mapping_axis_baselines.size()) {
				if (settled && !_mapping_axis_baseline_set[index]) {
					_mapping_axis_baseline_set[index] = true;
					_mapping_axis_baselines[index] = value;
				}
				baseline = _mapping_axis_baseline_set[index] ? _mapping_axis_baselines[index] : value;
			}
			/* Triggers rest at -32767 and close at +32767, so the delta reaches
			 * 65534 and must not be computed in int16_t (it would wrap to -2,
			 * which never passes the engagement threshold). */
			int delta = static_cast<int>(value) - static_cast<int>(baseline);

			if (!_mapping_axis_engaged) {
				if (std::abs(delta) >= _mapping_axis_threshold) {
					_mapping_axis_engaged = true;
					_mapping_axis_candidate = index;
				}
			} else if (index == _mapping_axis_candidate &&
					std::abs(delta) <= _mapping_axis_threshold / 2) {
				RecordAxisAndAdvance();
				AdvanceMappingStep();
			}
			break;
		}
	}
}
