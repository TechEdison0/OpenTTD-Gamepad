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

/** Activate the currently focused widget. */
void ActivateGamepadFocus();

/** After activation, transfer focus to a newly opened window if one appeared. */
void UpdateGamepadFocusAfterActivation();

/** Mark that a gamepad activation selected a dropdown row. */
void MarkGamepadDropdownSelection();

/** Focus the window opened by a gamepad dropdown selection. */
void NotifyGamepadDropdownSelectionCompleted();

/** Close the focused window through the normal back/escape path. */
void CloseGamepadFocusWindow();

/** Leave focus mode and return control to the left-stick crosshair. */
void ClearGamepadFocus();

/** Get the focused window and widget. @return false when focus mode is inactive. */
bool GetGamepadFocus(Window *&w, WidgetID &widget);

/** Whether a widget is currently focus-selected. */
bool IsGamepadFocusActive();

#endif /* GAMEPAD_FOCUS_H */
