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
	GAP_LSTICK  = 6,  ///< L3, combined with R3 for pause.
	GAP_RSTICK  = 7,  ///< R3, combined with L3 for pause.
};

static InputSource _input_source = InputSource::KeyboardMouse;
static bool _suppress_input_source_mark = false;
static std::array<bool, 18> _action_held = {};
static bool _pause_held = false;

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

void HandleGamepadActions()
{
	if (GetGamepadSelection() < 0) return;

	std::array<bool, 18> active = {};
	for (size_t i = 0; i < active.size(); i++) {
		active[i] = IsControllerMappingControlActive(i);
	}

	std::array<bool, 18> edge = {};
	bool any_edge = false;
	for (size_t i = 0; i < active.size(); i++) {
		edge[i] = active[i] && !_action_held[i];
		if (edge[i]) any_edge = true;
	}

	/* Pause is a chord, not either stick press alone. R3 alone toggles fast forward. */
	bool pause = active[GAP_LSTICK] && active[GAP_RSTICK];
	bool pause_edge = pause && !_pause_held;
	bool fastforward_edge = edge[GAP_RSTICK] && !active[GAP_LSTICK];
	_pause_held = pause;
	_action_held = active;

	if (any_edge) _input_source = InputSource::Gamepad;
	if (!any_edge && !pause_edge) return;

	/* B close, D-pad focus, stick crosshair/pan and trigger zoom remain placeholders
	 * until their respective C3/C5 chunks. Only the focus-independent actions dispatch. */
	if (edge[GAP_A]) HandleGamepadClick(false);
	if (edge[GAP_X]) HandleGamepadClick(true);
	if (edge[GAP_Y]) CancelToolWithGamepad();
	if (pause_edge) HandleToolbarHotkey(MTHK_PAUSE);
	if (fastforward_edge) HandleToolbarHotkey(MTHK_FASTFORWARD);
}
