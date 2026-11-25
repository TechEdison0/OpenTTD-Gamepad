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

/** @file controller_gui.cpp Controller selection and mapping window (chunk C1b: UI only). */

#include "stdafx.h"

#include "controller_gui.h"
#include "gamepad/gamepad.h"
#include "gamepad/controller_config.h"
#include "window_gui.h"
#include "window_func.h"
#include "gfx_func.h"
#include "palette_func.h"
#include "dropdown_type.h"
#include "dropdown_func.h"
#include "strings_func.h"
#include "table/strings.h"

#include <string>

#include "safeguards.h"

/** Widgets of the controller window. */
enum ControllerWidgets {
	WID_CT_CAPTION,  ///< Window caption.
	WID_CT_LABEL,    ///< "Selected Controller:" label.
	WID_CT_DROPDOWN, ///< Controller selection dropdown.
	WID_CT_MAP,      ///< "Map Controller" button.
	WID_CT_LOAD,     ///< "Load" button.
	WID_CT_SAVE,     ///< "Save" button.
	WID_CT_LIST,     ///< Plaintext mapping list.
};

static constexpr std::initializer_list<NWidgetPart> _controller_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_CT_CAPTION), SetStringTip(STR_CONTROLLER_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	/* Background panel: without it the custom EMPTY widgets leave uncovered
	 * screen area behind, which smears whatever was drawn before. */
	NWidget(WWT_PANEL, Colours::Grey),
		NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.frametext),
			NWidget(WWT_EMPTY, Colours::Invalid, WID_CT_LABEL), SetMinimalSize(280, 10), SetFill(1, 0),
			NWidget(WWT_DROPDOWN, Colours::Grey, WID_CT_DROPDOWN), SetMinimalSize(280, 12), SetStringTip(STR_CONTROLLER_SELECT, STR_NULL), SetFill(1, 0),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
				NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CT_MAP), SetMinimalSize(90, 12), SetStringTip(STR_CONTROLLER_MAP, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CT_LOAD), SetMinimalSize(60, 12), SetStringTip(STR_CONTROLLER_LOAD, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CT_SAVE), SetMinimalSize(60, 12), SetStringTip(STR_CONTROLLER_SAVE, STR_NULL),
			EndContainer(),
			NWidget(WWT_EMPTY, Colours::Invalid, WID_CT_LIST), SetMinimalSize(280, 120), SetFill(1, 1),
		EndContainer(),
	EndContainer(),
};

/** Window description of the controller settings window. */
static WindowDesc _controller_desc(
	WindowPosition::Center, "controller", 0, 0,
	WindowClass::Controller, WindowClass::None,
	{},
	_controller_widgets
);

/**
 * Controller selection and mapping window. Chunk C1b ships the layout only; device
 * population, config persistence and the mapping flow follow in C1c/C1d.
 */
struct ControllerWindow : Window {
	ControllerWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_CT_DROPDOWN: {
				DropDownList list;
				size_t count = GetGamepadCount();
				if (count == 0) {
					list.push_back(MakeDropDownListStringItem(std::string{GetString(STR_CONTROLLER_NONE)}, 0));
				} else {
					for (size_t i = 0; i < count; i++) {
						GamepadInfo info = GetGamepadInfo(i);
						std::string label = info.name.empty() ? std::string{GetString(STR_CONTROLLER_NONE)} : info.name;
						list.push_back(MakeDropDownListStringItem(std::move(label), static_cast<int>(i)));
					}
				}
				int selected = GetGamepadSelection();
				ShowDropDownList(this, std::move(list), selected >= 0 ? selected : 0, WID_CT_DROPDOWN);
				break;
			}

			case WID_CT_LOAD:
				LoadControllerConfig();
				this->SetDirty();
				break;

			case WID_CT_SAVE:
				SaveControllerConfig();
				this->SetDirty();
				break;

			default: break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, [[maybe_unused]] int click_result) override
	{
		if (widget != WID_CT_DROPDOWN) return;
		SetGamepadSelection(index >= 0 && static_cast<size_t>(index) < GetGamepadCount() ? index : -1);
		this->SetDirty();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget == WID_CT_LABEL) {
			std::string name = std::string{GetString(STR_CONTROLLER_NONE)};
			int selected = GetGamepadSelection();
			if (selected >= 0 && static_cast<size_t>(selected) < GetGamepadCount()) {
				name = GetGamepadInfo(static_cast<size_t>(selected)).name;
				if (name.empty()) name = std::string{GetString(STR_CONTROLLER_NONE)};
			}
			DrawString(r.left, r.right, r.top, GetString(STR_CONTROLLER_SELECTED, name),
				TextColour::White, AlignmentH::Start, false, FontSize::Normal);
		} else if (widget == WID_CT_LIST) {
			GfxFillRect(r, PC_BLACK);
			DrawString(r.left, r.right, r.top, "(no mapping)", TextColour::White, AlignmentH::Start, false, FontSize::Small);
		}
	}
};

void ShowControllerWindow()
{
	CloseWindowByClass(WindowClass::Controller);
	new ControllerWindow(_controller_desc, 0);
}
