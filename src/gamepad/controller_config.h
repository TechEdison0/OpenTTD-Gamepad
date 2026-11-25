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

/** @file controller_config.h Persistence for controller selection and mappings. */

#ifndef GAMEPAD_CONTROLLER_CONFIG_H
#define GAMEPAD_CONTROLLER_CONFIG_H

#include <string>

/** Path of controller.cfg, set during filesystem setup beside hotkeys.cfg. */
extern std::string _controller_file;

/**
 * Load the persisted controller selection from controller.cfg.
 *
 * Resolves the saved GUID against the connected controllers. When the selected
 * controller has no mapping entry yet, a fresh (empty) one is written immediately,
 * so a controller always has a section to fill in later.
 *
 * @return The resolved selection index, or -1 when nothing valid is selected.
 */
int LoadControllerConfig();

/**
 * Persist the current selection and the selected controller's mapping list.
 * Mappings are empty until the mapping flow lands (C1d).
 */
void SaveControllerConfig();

/** Result of the last load/save, shown in the window while diagnosing C1c. */
std::string GetControllerConfigStatus();

#endif /* GAMEPAD_CONTROLLER_CONFIG_H */
