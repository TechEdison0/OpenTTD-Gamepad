/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, version 2.
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

/** A activates the focused widget. */
void ActivateGamepadFocus();

/** Close the focused window through the normal back/escape path. */
void CloseGamepadFocusWindow();

/** Leave focus mode and return control to the left-stick crosshair. */
void ClearGamepadFocus();

/** Get the focused window and widget. @return false when focus mode is inactive. */
bool GetGamepadFocus(Window *&w, WidgetID &widget);

/** Whether a widget is currently focus-selected. */
bool IsGamepadFocusActive();

#endif /* GAMEPAD_FOCUS_H */
