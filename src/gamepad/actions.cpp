/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD.
 * If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file actions.cpp Normalized gamepad actions and input-source tracking. */

#include "../stdafx.h"
#include "actions.h"
#include "controller_config.h"
#include "gamepad.h"
#include "../gfx_func.h"
#include "../toolbar_gui.h"
#include "../window_func.h"

#include "../safeguards.h"

/** Mapping slots used by the C2 action dispatch. */
enum GamepadActionStep : size_t {
	GAP_A       = 0,  ///< Primary activate.
	GAP_B       = 1,  ///< Placeholder until C3 focus exists.
	GAP_X       = 2,  ///< Secondary activate.
	GAP_Y       = 3,  ///< Cancel tool / abort build.
	GAP_LSHOULDER = 4, ///< Held Ctrl modifier.
	GAP_RSHOULDER = 5, ///< Held Shift modifier.
	GAP_LSTICK  = 6,  ///< L3, combined with R3 for pause.
	GAP_RSTICK  = 7,  ///< R3, combined with L3 for pause.
};

static InputSource _input_source = InputSource::KeyboardMouse;
static bool _suppress_input_source_mark = false;
static std::array<bool, 18> _action_held = {};
static bool _left_held = false;
static bool _right_held = false;
static bool _ctrl_from_gamepad = false;
static bool _shift_from_gamepad = false;
static bool _ctrl_before_gamepad = false;
static bool _shift_before_gamepad = false;

void NotifyKeyboardMouseActivity()
{
	if (!_suppress_input_source_mark) _input_source = InputSource::KeyboardMouse;
}

InputSource GetInputSource()
{
	return _input_source;
}

/**
 * Cancel the active tool or text input. HandleBackButton() calls HandleKeypress(WKC_ESC),
 * which would mark keyboard activity, so suppress that mark for the duration of this call.
 */
static void CancelToolWithGamepad()
{
	_suppress_input_source_mark = true;
	HandleKeypress(WKC_ESC, 0);
	_suppress_input_source_mark = false;
}

/** Apply or release LB/RB as held Ctrl/Shift without losing the keyboard's own state. */
static void UpdateModifiers(const std::array<bool, 18> &active)
{
	if (active[GAP_LSHOULDER] && !_ctrl_from_gamepad) {
		_ctrl_before_gamepad = _ctrl_pressed;
		_ctrl_from_gamepad = true;
	} else if (!active[GAP_LSHOULDER] && _ctrl_from_gamepad) {
		_ctrl_pressed = _ctrl_before_gamepad;
		_ctrl_from_gamepad = false;
	}
	if (_ctrl_from_gamepad) _ctrl_pressed = true;

	if (active[GAP_RSHOULDER] && !_shift_from_gamepad) {
		_shift_before_gamepad = _shift_pressed;
		_shift_from_gamepad = true;
	} else if (!active[GAP_RSHOULDER] && _shift_from_gamepad) {
		_shift_pressed = _shift_before_gamepad;
		_shift_from_gamepad = false;
	}
	if (_shift_from_gamepad) _shift_pressed = true;
}

void HandleGamepadActions()
{
	if (GetGamepadSelection() < 0) return;

	std::array<bool, 18> active = {};
	for (size_t i = 0; i < active.size(); i++) {
		active[i] = IsControllerMappingControlActive(i);
	}

	std::array<bool, 18> edge = {};
	std::array<bool, 18> release = {};
	bool any_edge = false;
	for (size_t i = 0; i < active.size(); i++) {
		edge[i] = active[i] && !_action_held[i];
		release[i] = !active[i] && _action_held[i];
		if (edge[i]) any_edge = true;
	}

	/* LB/RB are held Ctrl/Shift modifiers, active from press until release. */
	UpdateModifiers(active);

	/* L3 pauses on its own; R3 alone toggles fast forward. */
	bool pause_edge = edge[GAP_LSTICK];
	bool fastforward_edge = edge[GAP_RSTICK];
	_action_held = active;

	if (any_edge) _input_source = InputSource::Gamepad;
	bool dispatch = _input_source == InputSource::Gamepad;

	/* B close, D-pad focus, stick crosshair/pan and trigger zoom remain placeholders
	 * until their respective C3/C5 chunks. Presses yield to active KB/M input, but
	 * releases always run: moving the mouse must not strand a held gamepad click. */
	if (dispatch && edge[GAP_A] && !_left_held) {
		HandleGamepadButton(false, true);
		_left_held = true;
	}
	if (release[GAP_A] && _left_held) {
		HandleGamepadButton(false, false);
		_left_held = false;
	}
	if (dispatch && edge[GAP_X] && !_right_held) {
		HandleGamepadButton(true, true);
		_right_held = true;
	}
	if (release[GAP_X] && _right_held) {
		HandleGamepadButton(true, false);
		_right_held = false;
	}
	if (dispatch && edge[GAP_Y]) CancelToolWithGamepad();
	if (dispatch && pause_edge) HandleToolbarHotkey(MTHK_PAUSE);
	if (dispatch && fastforward_edge) HandleToolbarHotkey(MTHK_FASTFORWARD);
}
