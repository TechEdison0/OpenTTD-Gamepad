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

/** @file gamepad.h Controller input, independent of the video driver. */

#ifndef GAMEPAD_GAMEPAD_H
#define GAMEPAD_GAMEPAD_H

/** Initialize controller support: enumerate devices and start tracking hotplug. */
void StartGamepads();

/** Release controller resources. */
void StopGamepads();

/**
 * Pump the SDL event queue. Called once per tick so controller events arrive no matter
 * which video driver is active (the native win32 driver never pumps SDL itself).
 */
void UpdateGamepads();

#endif /* GAMEPAD_GAMEPAD_H */
