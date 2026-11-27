/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD.
 * If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file focus.h C3 gamepad widget focus model. */

#ifndef GAMEPAD_FOCUS_H
#define GAMEPAD_FOCUS_H

#include "../window_type.h"

class Window;

/** Direction for geometric D-pad focus movement. */
enum class GamepadFocusDirection : uint8_t {
	Up,
	Down,
	Left,
	Right,
};

/** Move focus to the next focusable widget in the given direction. */
void MoveGamepadFocus(GamepadFocusDirection direction);

/** A activates the focused widget; returns the window that was activated. */
Window *ActivateGamepadFocus();

/** After activation, transfer focus to a newly opened window if one appeared. */
void UpdateGamepadFocusAfterActivation(Window *activated);

/** Force gamepad focus onto one specific window's first eligible widget. */
void FocusGamepadWindow(Window *w);

/** Focus the window opened by a gamepad dropdown selection. */
void NotifyGamepadDropdownSelectionCompleted(Window *dropdown_parent, WidgetID dropdown_button,
		bool dropdown_closed, Window *front_before, Window *front_after);

/** Close the focused window through the normal back/escape path. */
void CloseGamepadFocusWindow();

/** Leave focus mode and return control to the left-stick crosshair. */
void ClearGamepadFocus();

/** Get the focused window and widget. @return false when focus mode is inactive. */
bool GetGamepadFocus(Window *&w, WidgetID &widget);

/** Whether a widget is currently focus-selected. */
bool IsGamepadFocusActive();

#endif /* GAMEPAD_FOCUS_H */
