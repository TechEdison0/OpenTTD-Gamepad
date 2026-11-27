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

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "../safeguards.h"

std::string _controller_file;

/** Last action result, surfaced in the Controller window status line. */
static std::string _controller_status;

/** Current controller options, loaded with the selected controller. */
static ControllerOptions _controller_options;

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
 * decides which physical control feeds a slot.
 */
static constexpr std::array<const char *, _mapping_steps.size()> _mapping_descriptions = {{
	"Left click / hold-drag",
	"Close focused window",
	"Right click",
	"Esc: cancel tool / abort build",
	"Hold: Ctrl modifier",
	"Hold: Shift modifier",
	"L3: pause/resume",
	"R3: fast forward toggle",
	"Focus up",
	"Focus down",
	"Focus left",
	"Focus right",
	"Crosshair (C2)",
	"Crosshair (C2)",
	"Pan viewport",
	"Pan viewport",
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
/** High bit of a controller input event index marks an SDL logical game-controller axis. */
static constexpr uint8_t LOGICAL_AXIS_FLAG = 0x80;
/** Maximum raw axis index tracked during the A-prompt calibration gesture. */
static constexpr size_t MAX_CALIBRATION_AXES = 64;

/** Values captured so far, indexed like #_mapping_steps. */
static std::array<MappingValue, _mapping_steps.size()> _mapping_values;

/** Mapping flow state. */
enum class MappingPhase : uint8_t {
	Idle,        ///< Not mapping.
	AOnly,       ///< Waiting for A before showing the rest of the flow.
	Calibration, ///< A mapped; waiting for the stick/trigger gesture and A again.
	Full,        ///< All mapping steps visible and active.
};

static bool _mapping_active = false;
static MappingPhase _mapping_phase = MappingPhase::Idle;
static bool _mapping_use_logical = false;
static size_t _mapping_step = 0;
static uint32_t _mapping_step_started = 0;
/** Axis events before this tick are ignored after a capture. */
static uint32_t _mapping_axis_ignore_until = 0;

/** Min/max observed while the user performs the calibration gesture. */
struct AxisRangeSample {
	int16_t min = 32767;     ///< Lowest value observed during the gesture.
	int16_t max = -32768;    ///< Highest value observed during the gesture.
	int candidate = 0;       ///< Current run of identical values.
	uint32_t candidate_since = 0; ///< Start of the current run.
	int best = 0;            ///< Longest stable value observed.
	uint32_t best_duration = 0; ///< Duration of the longest stable run.
	bool seen = false;       ///< Whether any sample was taken.
};

static std::array<AxisRangeSample, MAX_CALIBRATION_AXES> _axis_range_samples;
static std::array<int16_t, MAX_CALIBRATION_AXES> _axis_rest = {};
static std::array<int16_t, MAX_CALIBRATION_AXES> _axis_max = {};
static std::array<bool, MAX_CALIBRATION_AXES> _axis_calibrated = {};
/** An axis can only be captured after an observed value near its learned rest. */
static std::array<bool, MAX_CALIBRATION_AXES> _axis_armed = {};

/** Feed one axis event into the A-prompt calibration gesture. */
static void SampleAxisRange(uint8_t axis, int value, uint32_t now)
{
	if (axis >= MAX_CALIBRATION_AXES) return;
	AxisRangeSample &sample = _axis_range_samples[axis];
	int16_t sample_value = static_cast<int16_t>(std::clamp(value, -32768, 32767));
	if (!sample.seen) {
		sample.seen = true;
		sample.min = sample_value;
		sample.max = sample_value;
		sample.candidate = value;
		sample.candidate_since = now;
		sample.best = value;
		sample.best_duration = 0;
		return;
	}
	sample.min = std::min(sample.min, sample_value);
	sample.max = std::max(sample.max, sample_value);
	if (value != sample.candidate) {
		sample.candidate = value;
		sample.candidate_since = now;
	}
	uint32_t duration = now - sample.candidate_since;
	if (duration > sample.best_duration) {
		sample.best_duration = duration;
		sample.best = value;
	}
}

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
		case MappingKind::Axis: return fmt::format("axis:{}:{}:{}:{}", value.index, value.min, value.max, value.rest);
		case MappingKind::LogicalAxis: return fmt::format("logical:{}:{}:{}:{}", value.index, value.min, value.max, value.rest);
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
static MappingValue ParseMappingValue(const std::string &text, uint8_t step_flag = MAPPING_STEP_AXIS)
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
		/* Older configs have no rest field; infer it from the step type. */
		value.rest = field(4).empty()
			? static_cast<int16_t>((step_flag & MAPPING_STEP_TRIGGER) != 0 ? -32768 : 0)
			: static_cast<int16_t>(ParseMappingNumber(field(4)));
	} else if (kind == "logical" && !field(1).empty() && !field(2).empty() && !field(3).empty()) {
		value.kind = MappingKind::LogicalAxis;
		value.index = static_cast<uint8_t>(ParseMappingNumber(field(1)));
		value.min = static_cast<int16_t>(ParseMappingNumber(field(2)));
		value.max = static_cast<int16_t>(ParseMappingNumber(field(3)));
		value.rest = field(4).empty() ? 0 : static_cast<int16_t>(ParseMappingNumber(field(4)));
	} else if (kind == "hat" && !field(1).empty() && !field(2).empty()) {
		value.kind = MappingKind::Hat;
		value.index = static_cast<uint8_t>(ParseMappingNumber(field(1)));
		value.direction = static_cast<uint8_t>(ParseMappingNumber(field(2)));
	}
	return value;
}

/** Description of a captured value for the window's mapping list. */
	/** Read a tolerant integer option from an INI group. */
static int ReadIniInt(const IniGroup *group, const char *key, int fallback)
{
	if (group == nullptr) return fallback;
	const IniItem *item = group->GetItem(key);
	if (item == nullptr || !item->value.has_value()) return fallback;
	return static_cast<int>(ParseMappingNumber(*item->value));
}

/** Read a boolean option from an INI group. */
static bool ReadIniBool(const IniGroup *group, const char *key, bool fallback)
{
	if (group == nullptr) return fallback;
	const IniItem *item = group->GetItem(key);
	if (item == nullptr || !item->value.has_value()) return fallback;
	std::string value = *item->value;
	return value == "1" || value == "true" || value == "yes";
}

/** Read a speed preset from an INI group. */
static ControllerAxisSpeed ReadIniSpeed(const IniGroup *group, const char *key)
{
	int value = std::clamp(ReadIniInt(group, key, static_cast<int>(ControllerAxisSpeed::Normal)), 0, 2);
	return static_cast<ControllerAxisSpeed>(value);
}

void LoadControllerOptions()
{
	_controller_options = ControllerOptions{};
	int index = GetGamepadSelection();
	if (index < 0 || static_cast<size_t>(index) >= GetGamepadCount()) return;

	IniFile ini{};
	ini.LoadFromDisk(_controller_file, Subdirectory::None);
	std::string section = "controller." + GetGamepadInfo(static_cast<size_t>(index)).guid;
	const IniGroup *device = ini.GetGroup(section);
	if (device == nullptr) return;

	_controller_options.invert_left_x = ReadIniBool(device, "invert_leftx", false);
	_controller_options.invert_left_y = ReadIniBool(device, "invert_lefty", false);
	_controller_options.invert_right_x = ReadIniBool(device, "invert_rightx", false);
	_controller_options.invert_right_y = ReadIniBool(device, "invert_righty", false);
	_controller_options.invert_left_trigger = ReadIniBool(device, "invert_ltrigger", false);
	_controller_options.invert_right_trigger = ReadIniBool(device, "invert_rtrigger", false);
	_controller_options.deadzone_left_stick = std::clamp(ReadIniInt(device, "deadzone_lstick", 15), 0, 100);
	_controller_options.deadzone_right_stick = std::clamp(ReadIniInt(device, "deadzone_rstick", 15), 0, 100);
	_controller_options.deadzone_left_trigger = std::clamp(ReadIniInt(device, "deadzone_ltrigger", 15), 0, 100);
	_controller_options.deadzone_right_trigger = std::clamp(ReadIniInt(device, "deadzone_rtrigger", 15), 0, 100);
	_controller_options.swap_sticks = ReadIniBool(device, "swap_sticks", false);
	_controller_options.left_stick_speed = ReadIniSpeed(device, "speed_lstick");
	_controller_options.right_stick_speed = ReadIniSpeed(device, "speed_rstick");
}

ControllerOptions &GetControllerOptions()
{
	return _controller_options;
}

bool GetControllerAxisInvert(size_t step)
{
	if (step >= _mapping_steps.size()) return false;
	const std::string &key = _mapping_steps[step].key;
	if (key == "leftx") return _controller_options.invert_left_x;
	if (key == "lefty") return _controller_options.invert_left_y;
	if (key == "rightx") return _controller_options.invert_right_x;
	if (key == "righty") return _controller_options.invert_right_y;
	if (key == "lefttrigger") return _controller_options.invert_left_trigger;
	if (key == "righttrigger") return _controller_options.invert_right_trigger;
	return false;
}

uint8_t GetControllerDeadzone(ControllerAxisGroup group)
{
	switch (group) {
		case ControllerAxisGroup::LeftStick:   return _controller_options.deadzone_left_stick;
		case ControllerAxisGroup::RightStick:  return _controller_options.deadzone_right_stick;
		case ControllerAxisGroup::LeftTrigger: return _controller_options.deadzone_left_trigger;
		case ControllerAxisGroup::RightTrigger:return _controller_options.deadzone_right_trigger;
	}
	return 15;
}

uint8_t GetControllerDeadzoneForStep(size_t step)
{
	if (step >= _mapping_steps.size()) return 15;
	const std::string &key = _mapping_steps[step].key;
	if (key == "leftx" || key == "lefty") return GetControllerDeadzone(ControllerAxisGroup::LeftStick);
	if (key == "rightx" || key == "righty") return GetControllerDeadzone(ControllerAxisGroup::RightStick);
	if (key == "lefttrigger") return GetControllerDeadzone(ControllerAxisGroup::LeftTrigger);
	if (key == "righttrigger") return GetControllerDeadzone(ControllerAxisGroup::RightTrigger);
	return 15;
}

ControllerAxisSpeed GetControllerStickSpeed(ControllerAxisGroup group)
{
	return group == ControllerAxisGroup::RightStick ? _controller_options.right_stick_speed : _controller_options.left_stick_speed;
}

int GetControllerSpeedMultiplier(ControllerAxisSpeed speed)
{
	switch (speed) {
		case ControllerAxisSpeed::Slow:   return 50;
		case ControllerAxisSpeed::Normal: return 100;
		case ControllerAxisSpeed::Fast:   return 200;
	}
	return 100;
}

bool GetControllerStickSwap()
{
	return _controller_options.swap_sticks;
}

static std::string DescribeMappingValue(const MappingValue &value)
{
	switch (value.kind) {
		case MappingKind::None: return {};
		case MappingKind::Button: return fmt::format("button {}", value.index);
		case MappingKind::Axis: return fmt::format("axis {} [{}, {}] rest {}", value.index, value.min, value.max, value.rest);
		case MappingKind::LogicalAxis: return fmt::format("logical axis {} [{}, {}] rest {}", value.index, value.min, value.max, value.rest);
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

		const ControllerOptions &opt = _controller_options;
		device.GetOrCreateItem("invert_leftx").SetValue(opt.invert_left_x ? "1" : "0");
		device.GetOrCreateItem("invert_lefty").SetValue(opt.invert_left_y ? "1" : "0");
		device.GetOrCreateItem("invert_rightx").SetValue(opt.invert_right_x ? "1" : "0");
		device.GetOrCreateItem("invert_righty").SetValue(opt.invert_right_y ? "1" : "0");
		device.GetOrCreateItem("invert_ltrigger").SetValue(opt.invert_left_trigger ? "1" : "0");
		device.GetOrCreateItem("invert_rtrigger").SetValue(opt.invert_right_trigger ? "1" : "0");
		device.GetOrCreateItem("deadzone_lstick").SetValue(fmt::format("{}", opt.deadzone_left_stick));
		device.GetOrCreateItem("deadzone_rstick").SetValue(fmt::format("{}", opt.deadzone_right_stick));
		device.GetOrCreateItem("deadzone_ltrigger").SetValue(fmt::format("{}", opt.deadzone_left_trigger));
		device.GetOrCreateItem("deadzone_rtrigger").SetValue(fmt::format("{}", opt.deadzone_right_trigger));
		device.GetOrCreateItem("swap_sticks").SetValue(opt.swap_sticks ? "1" : "0");
		device.GetOrCreateItem("speed_lstick").SetValue(fmt::format("{}", static_cast<int>(opt.left_stick_speed)));
		device.GetOrCreateItem("speed_rstick").SetValue(fmt::format("{}", static_cast<int>(opt.right_stick_speed)));
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

/** Rest value used when an axis was not seen during calibration. */
static int16_t DefaultAxisRest(uint8_t axis)
{
	return IsGamepadTriggerAxis(axis) ? -32768 : 0;
}

/** Store the axis just detected and move on. */
static void RecordAxisAndAdvance(uint8_t axis)
{
	int16_t rest = axis < MAX_CALIBRATION_AXES && _axis_calibrated[axis]
		? _axis_rest[axis] : DefaultAxisRest(axis);
	int16_t maximum = axis < MAX_CALIBRATION_AXES && _axis_calibrated[axis]
		? _axis_max[axis] : static_cast<int16_t>(32767);
	if (maximum <= rest) {
		int promoted = std::clamp(static_cast<int>(rest) + 32767, -32768, 32767);
		maximum = static_cast<int16_t>(promoted);
	}

	_mapping_values[_mapping_step] = MappingValue{MappingKind::Axis, axis, rest, maximum, rest, 0};
	_mapping_step++;
	ResetMappingStepState();
	_mapping_axis_ignore_until = SDL_GetTicks() + _mapping_axis_quiet_ms;
}

/** Move to the next step, finishing and saving when the last one completes. */
static void AdvanceMappingStep()
{
	if (_mapping_step >= _mapping_steps.size()) {
		_mapping_active = false;
		_mapping_phase = MappingPhase::Idle;
		_mapping_use_logical = false;
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
	LoadControllerOptions();
	_controller_status = fmt::format("loaded {} (index {})", _controller_file, index);

	/* Parse any stored mappings; missing keys simply stay unset. */
	_mapping_values.fill(MappingValue{});
	if (index >= 0) {
		std::string section = "controller." + GetGamepadInfo(static_cast<size_t>(index)).guid;
		if (const IniGroup *device = ini.GetGroup(section)) {
			for (size_t i = 0; i < _mapping_steps.size(); i++) {
				if (const IniItem *item = device->GetItem(_mapping_steps[i].key)) {
					if (item->value.has_value()) _mapping_values[i] = ParseMappingValue(*item->value, _mapping_steps[i].flag);
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

bool IsControllerMappingCalibrating()
{
	return _mapping_active && _mapping_phase == MappingPhase::Calibration;
}

std::string GetControllerMappingPrompt()
{
	if (!_mapping_active || _mapping_step >= _mapping_steps.size()) return {};
	if (_mapping_phase == MappingPhase::Calibration) {
		return "Move left / right stick in a circle, press and release LT / RT, "
				"release sticks and triggers, press A to continue";
	}
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
	size_t shown = _mapping_active && _mapping_phase != MappingPhase::Full ? 1 : _mapping_steps.size();
	for (size_t i = 0; i < shown; i++) {
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
			/* Use the calibrated rest captured at mapping time. Controllers such as the
			 * 8BitDo Ultimate MG can rest at -128 instead of 0 / -32768. */
			int rest = static_cast<int>(value.rest);
			if (_mapping_steps[step].flag == MAPPING_STEP_TRIGGER) {
				int range = static_cast<int>(value.max) - static_cast<int>(value.min);
				if (range <= 0) return false;
				return GetGamepadLiveAxis(value.index) > rest + range / 4;
			}
			int delta = static_cast<int>(GetGamepadLiveAxis(value.index)) - rest;
			return delta > _mapping_axis_threshold / 2 || delta < -_mapping_axis_threshold / 2;
		}

		case MappingKind::LogicalAxis: {
			int live = static_cast<int>(GetGamepadLogicalAxisValue(value.index));
			if (_mapping_steps[step].flag == MAPPING_STEP_TRIGGER) return live > value.max / 4;
			int delta = live - static_cast<int>(value.rest);
			return delta > _mapping_axis_threshold / 2 || delta < -_mapping_axis_threshold / 2;
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
	if (value.kind != MappingKind::Axis && value.kind != MappingKind::LogicalAxis) return 0;

	int minimum = static_cast<int>(value.min);
	int maximum = static_cast<int>(value.max);
	if (maximum <= minimum) return 0;
	int half_range = (maximum - minimum) / 2;
	if (half_range <= 0) return 0;
	int centre = static_cast<int>(value.rest);
	int live = value.kind == MappingKind::LogicalAxis
		? static_cast<int>(GetGamepadLogicalAxisValue(value.index))
		: static_cast<int>(GetGamepadLiveAxis(value.index));

	int scaled = (live - centre) * 100 / half_range;
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
	_mapping_use_logical = HasGameControllerAxisMapping(static_cast<size_t>(GetGamepadSelection()));
	_axis_range_samples.fill(AxisRangeSample{});
	_axis_calibrated.fill(false);
	_axis_armed.fill(false);
	_mapping_step = 0;
	_mapping_active = true;
	/* Mapped game controllers expose normalized axes; they need no custom gesture. */
	_mapping_phase = _mapping_use_logical ? MappingPhase::Full : MappingPhase::AOnly;
	_mapping_axis_ignore_until = 0;
	ResetMappingStepState();
	_controller_status = _mapping_use_logical ? "mapping: follow the prompts"
		: "mapping: press A, then perform the calibration gesture";
	InvalidateWindowData(WindowClass::Controller, 0);
	return true;
}

void CancelControllerMapping()
{
	_mapping_active = false;
	_mapping_phase = MappingPhase::Idle;
	_mapping_use_logical = false;
	_mapping_axis_ignore_until = 0;
	_controller_status = "mapping cancelled";
	InvalidateWindowData(WindowClass::Controller, 0);
}

/** Store an SDL-normalized game-controller axis and move on. */
static void RecordLogicalAxisAndAdvance(uint8_t logical_axis)
{
	bool trigger = logical_axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT;
	int16_t minimum = trigger ? 0 : -32768;
	int16_t maximum = 32767;
	_mapping_values[_mapping_step] = MappingValue{MappingKind::LogicalAxis, logical_axis, minimum, maximum, 0, 0};
	_mapping_step++;
	ResetMappingStepState();
	_mapping_axis_ignore_until = SDL_GetTicks() + _mapping_axis_quiet_ms;
}

void ControllerMappingEvent(ControllerInputEventType type, uint8_t index, int16_t value)
{
	if (!_mapping_active || _mapping_step >= _mapping_steps.size()) return;

	auto record_button = [&](uint8_t button) {
		_mapping_values[_mapping_step] = MappingValue{MappingKind::Button, button, -32768, 32767, 0, 0};
		_mapping_step++;
		ResetMappingStepState();
		AdvanceMappingStep();
	};

	bool settled = SDL_GetTicks() - _mapping_step_started >= _mapping_settle_ms;

	if (_mapping_phase == MappingPhase::AOnly) {
		if (settled && type == ControllerInputEventType::ButtonUp) record_button(index);
		if (_mapping_step != 1) return;

		/* A is mapped. Show only the gesture prompt until A is pressed again. */
		_mapping_phase = MappingPhase::Calibration;
		_axis_range_samples.fill(AxisRangeSample{});
		ResetMappingStepState();
		_controller_status = "mapping: calibrate sticks and triggers, then press A";
		InvalidateWindowData(WindowClass::Controller, 0);
		return;
	}

	if (_mapping_phase == MappingPhase::Calibration) {
		if (type == ControllerInputEventType::AxisMotion && IsSelectedControllerAxis(index) &&
				(IsGamepadStickAxis(index) || IsGamepadTriggerAxis(index))) {
			SampleAxisRange(index, static_cast<int>(value), SDL_GetTicks());
		}
		bool a_released = type == ControllerInputEventType::ButtonUp &&
				_mapping_values[0].kind == MappingKind::Button && index == _mapping_values[0].index;
		if (!settled || !a_released) return;

		for (size_t i = 0; i < MAX_CALIBRATION_AXES; i++) {
			const AxisRangeSample &sample = _axis_range_samples[i];
			if (!sample.seen) continue;
			int range = static_cast<int>(sample.max) - static_cast<int>(sample.min);
			if (range < _mapping_axis_threshold) continue;
			/* Rest is the longest stable value during the gesture. Some pads rest
			 * near -128 while their full travel reaches -32767; a brief press must
			 * not replace that resting baseline. */
			_axis_rest[i] = static_cast<int16_t>(std::clamp(sample.best, -32768, 32767));
			_axis_max[i] = sample.max;
			_axis_calibrated[i] = true;
		}
		_mapping_phase = MappingPhase::Full;
		ResetMappingStepState();
		/* The gesture and the confirming A release can leave axis events in flight;
		 * do not let those finish the first axis prompt for the user. */
		_mapping_axis_ignore_until = SDL_GetTicks() + _mapping_axis_quiet_ms;
		_controller_status = "mapping: follow the prompts";
		InvalidateWindowData(WindowClass::Controller, 0);
		return;
	}

	const MappingStepDef &step = _mapping_steps[_mapping_step];
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
				_mapping_values[_mapping_step] = MappingValue{MappingKind::Hat, index, -32768, 32767, 0, static_cast<uint8_t>(value)};
				_mapping_step++;
				ResetMappingStepState();
				AdvanceMappingStep();
			}
			break;

		case ControllerInputEventType::AxisMotion: {
			if (step.flag != MAPPING_STEP_AXIS && step.flag != MAPPING_STEP_TRIGGER) break;

			uint32_t now = SDL_GetTicks();
			/* Time-based ignore: no dependency on further axis events, so mapping
			 * cannot freeze if the user releases a stick and waits. */
			if (now < _mapping_axis_ignore_until) break;
			if (!settled) break;

			if (_mapping_use_logical) {
				if ((index & LOGICAL_AXIS_FLAG) == 0) break;
				uint8_t logical = static_cast<uint8_t>(index & 0x7F);
				if (logical > SDL_CONTROLLER_AXIS_TRIGGERRIGHT) break;
				if (logical >= SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
					if (value <= 32767 / 4) break;
				} else if (std::abs(static_cast<int>(value)) <= _mapping_axis_threshold / 2) {
					break;
				}
				RecordLogicalAxisAndAdvance(logical);
				AdvanceMappingStep();
				break;
			}

			/* Raw path: SDL did not provide normalized game-controller axes. */
			if ((index & LOGICAL_AXIS_FLAG) != 0) break;
			if (!IsSelectedControllerAxis(index)) break;
			if (index >= MAX_CALIBRATION_AXES || !_axis_calibrated[index]) break;

			int rest = static_cast<int>(_axis_rest[index]);
			int delta = std::abs(static_cast<int>(value) - rest);
			if (delta <= _mapping_deadzone) {
				/* Observed at rest: arm this axis for the next deliberate movement. */
				_axis_armed[index] = true;
				break;
			}
			if (!_axis_armed[index] || delta <= _mapping_axis_threshold) break;
			_axis_armed[index] = false;
			RecordAxisAndAdvance(index);
			AdvanceMappingStep();
			break;
		}
	}
}
