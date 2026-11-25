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

#include <array>
#include <string>

#include "../safeguards.h"

std::string _controller_file;

/** Mapping keys stored per controller; values stay empty until C1d fills them. */
static constexpr std::array<const char *, 20> _mapping_keys = {
	"a", "b", "x", "y",
	"leftshoulder", "rightshoulder", "back", "start", "guide",
	"leftstick", "rightstick", "dpup", "dpdown", "dpleft", "dpright",
	"leftx", "lefty", "rightx", "righty", "triggers",
};

/** Write the selection plus the selected controller's (currently empty) mapping list. */
static void WriteConfigFile(const std::string &selected_guid)
{
	IniFile ini{};
	ini.LoadFromDisk(_controller_file, Subdirectory::None);

	IniGroup &settings = ini.GetOrCreateGroup("controller");
	settings.GetOrCreateItem("selected").SetValue(selected_guid);

	if (!selected_guid.empty()) {
		IniGroup &device = ini.GetOrCreateGroup("controller." + selected_guid);
		for (const char *key : _mapping_keys) {
			device.GetOrCreateItem(key).SetValue("");
		}
	}

	ini.SaveToDisk(_controller_file);
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

	/* No entry yet for this controller: create a fresh, empty one. */
	if (index >= 0) {
		std::string section = "controller." + GetGamepadInfo(index).guid;
		if (ini.GetGroup(section) == nullptr) WriteConfigFile(GetGamepadInfo(index).guid);
	}

	return index;
}

void SaveControllerConfig()
{
	std::string guid;
	if (int index = GetGamepadSelection(); index >= 0 && static_cast<size_t>(index) < GetGamepadCount()) {
		guid = GetGamepadInfo(static_cast<size_t>(index)).guid;
	}
	WriteConfigFile(guid);
}
