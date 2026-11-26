/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD.
 * If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file actions.h Normalized gamepad actions and input-source tracking. */

#ifndef GAMEPAD_ACTIONS_H
#define GAMEPAD_ACTIONS_H

/** Which input device last produced deliberate activity. */
enum class InputSource : uint8_t {
	KeyboardMouse, ///< Keyboard or mouse is in control.
	Gamepad,       ///< Controller is in control.
};

/** Mark real keyboard or mouse activity, yielding control from the gamepad. */
void NotifyKeyboardMouseActivity();

/** Current input source. */
InputSource GetInputSource();

/** Gamepad-contributed modifier state; OR with the keyboard state in the video driver. */
void GetGamepadModifiers(bool &ctrl, bool &shift);

/** Move the crosshair by a pixel delta. C3 D-pad snapping reuses this helper. */
void MoveGamepadCursor(int delta_x, int delta_y);

/** Whether the last cursor delta came from the gamepad, so KB/M activity must not be marked. */
bool ConsumeGamepadCursorMovement();

/** Consume mapped controller actions. Called once per tick from InputLoop(). */
void HandleGamepadActions();

#endif /* GAMEPAD_ACTIONS_H */
