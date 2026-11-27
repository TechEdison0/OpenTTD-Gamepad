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
#include "focus.h"
#include "gamepad.h"
#include "../gfx_func.h"
#include "../toolbar_gui.h"
#include "../window_func.h"
#include "../window_gui.h"

#include <cmath>

#include "../safeguards.h"

/** Mapping slots used by the C2 action dispatch. */
enum GamepadActionStep : size_t {
	GAP_A       = 0,  ///< Primary activate.
	GAP_B       = 1,  ///< Close focused window.
	GAP_X       = 2,  ///< Secondary activate.
	GAP_Y       = 3,  ///< Cancel tool / abort build.
	GAP_LSHOULDER = 4, ///< Held Ctrl modifier.
	GAP_RSHOULDER = 5, ///< Held Shift modifier.
	GAP_LSTICK  = 6,  ///< L3, pauses.
	GAP_RSTICK  = 7,  ///< R3, toggles fast forward.
	GAP_DPAD_UP = 8,   ///< D-pad up, focus navigation.
	GAP_DPAD_DOWN = 9, ///< D-pad down, focus navigation.
	GAP_DPAD_LEFT = 10,///< D-pad left, focus navigation.
	GAP_DPAD_RIGHT = 11,///< D-pad right, focus navigation.
	GAP_LTRIGGER = 16, ///< LT, zoom out.
	GAP_RTRIGGER = 17, ///< RT, zoom in.
	GAP_LSTICK_X = 12, ///< Left stick horizontal, crosshair.
	GAP_LSTICK_Y = 13, ///< Left stick vertical, crosshair.
	GAP_RSTICK_X = 14, ///< Right stick horizontal, viewport pan.
	GAP_RSTICK_Y = 15, ///< Right stick vertical, viewport pan.
};

static InputSource _input_source = InputSource::KeyboardMouse;
static bool _suppress_input_source_mark = false;
static std::array<bool, 18> _action_held = {};
static bool _left_held = false;
static bool _right_held = false;
static bool _ctrl_from_gamepad = false;
static bool _shift_from_gamepad = false;
static bool _gamepad_cursor_moved = false;
static bool _gamepad_virtual_click = false;
static bool _touch_activity_pending = false;
static Point _saved_gamepad_cursor{};
static bool _has_saved_gamepad_cursor = false;

bool IsGamepadVirtualClick()
{
	return _gamepad_virtual_click;
}

void SetGamepadVirtualClick(bool active)
{
	_gamepad_virtual_click = active;
}

void NotifyKeyboardMouseActivity()
{
	if (!_suppress_input_source_mark) {
		_input_source = InputSource::KeyboardMouse;
		ClearGamepadFocus();
		DiscardGamepadCursorPosition();
	}
}

void NotifyTouchActivity()
{
	if (_suppress_input_source_mark) return;
	_input_source = InputSource::Touch;
	_touch_activity_pending = true;
	ClearGamepadFocus();
	DiscardGamepadCursorPosition();
}

bool ConsumeTouchActivityMark()
{
	bool pending = _touch_activity_pending;
	_touch_activity_pending = false;
	return pending;
}

void CloseGamepadFocusWindow()
{
	_suppress_input_source_mark = true;
	HandleBackButton();
	_suppress_input_source_mark = false;
	ClearGamepadFocus();
	RestoreGamepadCursorPosition();
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

/** Track held LB/RB. The video driver ORs these with keyboard state so Ctrl/Shift
 * changes are signalled once by HandleCtrlChanged(), not toggled every frame. */
static void UpdateModifiers(const std::array<bool, 18> &active)
{
	_ctrl_from_gamepad = active[GAP_LSHOULDER];
	_shift_from_gamepad = active[GAP_RSHOULDER];
}

void GetGamepadModifiers(bool &ctrl, bool &shift)
{
	ctrl = _ctrl_from_gamepad;
	shift = _shift_from_gamepad;
}

void ApplyGamepadStickResponse(int x, int y, int speed, int &out_x, int &out_y)
{
	out_x = 0;
	out_y = 0;

	/* Scaled radial deadzone: rescale after removing the deadzone so the stick does
	 * not jump from 0 to full sensitivity at the edge. Then gamma 2 keeps the first
	 * quarter of travel fine and ramps the remaining three quarters quickly. */
	static constexpr double DEADZONE = 15.0;
	double magnitude = std::sqrt(static_cast<double>(x * x + y * y));
	if (magnitude <= DEADZONE) return;

	double scaled = (magnitude - DEADZONE) * 100.0 / (100.0 - DEADZONE);
	if (scaled > 100.0) scaled = 100.0;
	double curved = scaled * scaled / 100.0;
	double output = curved * speed / 100.0;
	double ratio = output / magnitude;
	out_x = static_cast<int>(std::lround(x * ratio));
	out_y = static_cast<int>(std::lround(y * ratio));
}

void MoveGamepadCursor(int delta_x, int delta_y)
{
	int new_x = _cursor.pos.x + delta_x;
	int new_y = _cursor.pos.y + delta_y;
	if (new_x < 0) new_x = 0;
	if (new_y < 0) new_y = 0;
	if (new_x >= _screen.width) new_x = _screen.width - 1;
	if (new_y >= _screen.height) new_y = _screen.height - 1;

	delta_x = new_x - _cursor.pos.x;
	delta_y = new_y - _cursor.pos.y;
	if (delta_x == 0 && delta_y == 0) return;

	_cursor.pos.x = new_x;
	_cursor.pos.y = new_y;
	_cursor.delta.x = delta_x;
	_cursor.delta.y = delta_y;
	_cursor.in_window = true;
	_cursor.dirty = true;
	_gamepad_cursor_moved = true;
}

bool ConsumeGamepadCursorMovement()
{
	bool moved = _gamepad_cursor_moved;
	_gamepad_cursor_moved = false;
	return moved;
}

void SetGamepadCursorPosition(int x, int y)
{
	int delta_x = x - _cursor.pos.x;
	int delta_y = y - _cursor.pos.y;
	_cursor.pos.x = x;
	_cursor.pos.y = y;
	_cursor.delta.x = delta_x;
	_cursor.delta.y = delta_y;
	_cursor.in_window = true;
	_cursor.dirty = true;
	_gamepad_cursor_moved = true;
}

void SaveGamepadCursorPosition()
{
	_saved_gamepad_cursor = _cursor.pos;
	_has_saved_gamepad_cursor = true;
}

void RestoreGamepadCursorPosition()
{
	if (!_has_saved_gamepad_cursor) return;
	SetGamepadCursorPosition(_saved_gamepad_cursor.x, _saved_gamepad_cursor.y);
	_has_saved_gamepad_cursor = false;
}

void DiscardGamepadCursorPosition()
{
	_has_saved_gamepad_cursor = false;
}

void HandleGamepadActions()
{
	if (GetGamepadSelection() < 0) return;

	std::array<bool, 18> active = {};
	for (size_t i = 0; i < active.size(); i++) {
		active[i] = IsControllerMappingControlActive(i);
	}

	/* Left stick: adjust a focused slider in place, otherwise drive the crosshair. */
	static constexpr int CROSSHAIR_SPEED = 12; // pixels per tick at full deflection
	static constexpr int SLIDER_SPEED = 1;     // slider steps per tick at full deflection
	int delta_x = 0;
	int delta_y = 0;
	ApplyGamepadStickResponse(GetControllerMappingAxisValue(GAP_LSTICK_X),
			GetControllerMappingAxisValue(GAP_LSTICK_Y), CROSSHAIR_SPEED, delta_x, delta_y);

	Window *focus_w = nullptr;
	WidgetID focus_widget = -1;
	int slider_min = 0;
	int slider_max = 0;
	int slider_value = 0;
	bool on_slider = GetGamepadFocus(focus_w, focus_widget) &&
			focus_w->GetGamepadSliderInfo(focus_widget, slider_min, slider_max, slider_value);

	if (on_slider) {
		int slider_delta = 0;
		int unused = 0;
		ApplyGamepadStickResponse(GetControllerMappingAxisValue(GAP_LSTICK_X), 0, SLIDER_SPEED, slider_delta, unused);
		if (slider_delta != 0) {
			focus_w->OnGamepadSliderDelta(focus_widget, slider_delta);
			_input_source = InputSource::Gamepad;
		}
	} else if (delta_x != 0 || delta_y != 0) {
		RestoreGamepadCursorPosition();
		MoveGamepadCursor(delta_x, delta_y);
		ClearGamepadFocus();
		_input_source = InputSource::Gamepad;
	}

	/* Right stick pans the main viewport. It is independent of focus and the left-stick
	 * crosshair, so a build window can stay focused while the map moves. */
	static constexpr int PAN_SPEED = 12; // pixels per tick at full deflection
	int pan_x = 0;
	int pan_y = 0;
	ApplyGamepadStickResponse(GetControllerMappingAxisValue(GAP_RSTICK_X),
			GetControllerMappingAxisValue(GAP_RSTICK_Y), PAN_SPEED, pan_x, pan_y);
	if (pan_x != 0 || pan_y != 0) {
		HandleJoystickScroll({pan_x, pan_y});
		_input_source = InputSource::Gamepad;
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
	/* One zoom step per press; the keyboard hotkeys handle their own auto-repeat. */
	bool zoom_in_edge = edge[GAP_RTRIGGER];
	bool zoom_out_edge = edge[GAP_LTRIGGER];
	_action_held = active;

	if (any_edge) _input_source = InputSource::Gamepad;
	bool dispatch = _input_source == InputSource::Gamepad;

	/* C3 focus navigation. Presses yield to active KB/M input, but releases always run:
	 * moving the mouse must not strand a held gamepad click. */
	if (dispatch && edge[GAP_DPAD_UP]) MoveGamepadFocus(GamepadFocusDirection::Up);
	if (dispatch && edge[GAP_DPAD_DOWN]) MoveGamepadFocus(GamepadFocusDirection::Down);
	if (dispatch && edge[GAP_DPAD_LEFT]) MoveGamepadFocus(GamepadFocusDirection::Left);
	if (dispatch && edge[GAP_DPAD_RIGHT]) MoveGamepadFocus(GamepadFocusDirection::Right);

	if (dispatch && edge[GAP_A] && !_left_held) {
		if (IsGamepadFocusActive()) {
			ActivateGamepadFocus();
		} else {
			HandleGamepadButton(false, true);
			_left_held = true;
		}
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
	if (dispatch && edge[GAP_B]) CloseGamepadFocusWindow();
	if (dispatch && edge[GAP_Y]) CancelToolWithGamepad();
	if (dispatch && pause_edge) HandleToolbarHotkey(MTHK_PAUSE);
	if (dispatch && fastforward_edge) HandleToolbarHotkey(MTHK_FASTFORWARD);
	if (dispatch && zoom_in_edge) HandleToolbarHotkey(MTHK_ZOOM_IN);
	if (dispatch && zoom_out_edge) HandleToolbarHotkey(MTHK_ZOOM_OUT);
}
