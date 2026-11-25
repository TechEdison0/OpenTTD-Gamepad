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

/** @file controller_gui.cpp Controller selection and mapping window (chunk C1). */

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
	WID_CT_DROPDOWN, ///< Controller selection dropdown (caption shows the selection).
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
			NWidget(WWT_DROPDOWN, Colours::Grey, WID_CT_DROPDOWN), SetMinimalSize(380, 12), SetStringTip(STR_CONTROLLER_SELECT, STR_NULL), SetFill(1, 0),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
				NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CT_MAP), SetMinimalSize(90, 12), SetStringTip(STR_CONTROLLER_MAP, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CT_LOAD), SetMinimalSize(60, 12), SetStringTip(STR_CONTROLLER_LOAD, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CT_SAVE), SetMinimalSize(60, 12), SetStringTip(STR_CONTROLLER_SAVE, STR_NULL),
			EndContainer(),
			NWidget(WWT_EMPTY, Colours::Invalid, WID_CT_LIST), SetMinimalSize(460, 260), SetFill(1, 1),
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

/** Name of the currently selected controller, or "none". */
static std::string ControllerSelectedName()
{
	int selected = GetGamepadSelection();
	if (selected >= 0 && static_cast<size_t>(selected) < GetGamepadCount()) {
		std::string name = GetGamepadInfo(static_cast<size_t>(selected)).name;
		if (!name.empty()) return name;
	}
	return std::string{GetString(STR_CONTROLLER_NONE)};
}

/**
 * Controller selection and mapping window. Chunk C1c wires the device registry and
 * controller.cfg; the mapping flow follows in C1d.
 */
struct ControllerWindow : Window {
private:
	uint accumulated_ms = 0; ///< Time since the last live-state repaint.

public:
	ControllerWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		this->accumulated_ms += delta_ms;
		if (this->accumulated_ms >= 100) {
			this->accumulated_ms = 0;
			this->SetDirty();
		}
	}

	void OpenControllerList()
	{
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
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_CT_DROPDOWN) return ControllerSelectedName();
		if (widget == WID_CT_MAP && IsControllerMapping()) return std::string{GetString(STR_CONTROLLER_CANCEL)};
		return Window::GetWidgetString(widget, stringid);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		switch (widget) {
			case WID_CT_DROPDOWN:
				if (!IsControllerMapping()) this->OpenControllerList();
				break;

			case WID_CT_MAP:
				if (IsControllerMapping()) CancelControllerMapping(); else StartControllerMapping();
				this->SetDirty();
				break;

			case WID_CT_LOAD:
				if (IsControllerMapping()) break;
				LoadControllerConfig();
				this->SetDirty();
				break;

			case WID_CT_SAVE:
				if (IsControllerMapping()) break;
				SaveControllerConfig();
				this->SetDirty();
				break;

			default: break;
		}
	}

	void OnDropdownSelect(WidgetID widget, int index, [[maybe_unused]] int click_result) override
	{
		if (widget != WID_CT_DROPDOWN || IsControllerMapping()) return;
		SetGamepadSelection(index >= 0 && static_cast<size_t>(index) < GetGamepadCount() ? index : -1);
		this->SetDirty();
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_CT_LIST) return;
		GfxFillRect(r, PC_BLACK);

		/* Column boundaries are fractions of the widget, not fixed pixels: the window is
		 * scaled on high-DPI devices, so a static offset clips text on Android. Small
		 * text keeps the longest axis value readable in the middle column. */
		const int header_height = GetCharacterHeight(FontSize::Normal);
		const int row_height = GetCharacterHeight(FontSize::Small);
		const int list_width = r.right - r.left;
		const int label_end = r.left + list_width * 38 / 100;
		const int value_end = r.left + list_width * 76 / 100;
		int y = r.top;

		DrawString(r.left, r.right, y, "Control / Mapping / Live", TextColour::Silver, AlignmentH::Start, false, FontSize::Normal);
		y += header_height;

		std::string prompt = GetControllerMappingPrompt();
		if (!prompt.empty()) {
			DrawString(r.left, r.right, y, prompt, TextColour::Yellow, AlignmentH::Start, false, FontSize::Normal);
			y += header_height;
		}

		auto rows = GetControllerMappingRows();
		size_t current = rows.size();
		bool any_set = false;
		for (size_t i = 0; i < rows.size(); i++) {
			if (rows[i].second == "—") {
				current = i; /* the step currently being captured */
				break;
			}
			any_set = true;
		}

		if (!any_set && prompt.empty()) {
			DrawString(r.left, r.right, y, "(no mapping)", TextColour::White, AlignmentH::Start, false, FontSize::Normal);
			y += header_height;
		} else {
			for (size_t i = 0; i < rows.size(); i++) {
				if (y + row_height > r.bottom) break;
				TextColour colour = rows[i].second == "—"
					? TextColour::Grey
					: (IsControllerMapping() && i == current ? TextColour::Yellow : TextColour::White);
				DrawString(r.left, label_end, y, rows[i].first, colour, AlignmentH::Start, false, FontSize::Small);
				DrawString(label_end, value_end, y, rows[i].second, colour, AlignmentH::Start, false, FontSize::Small);
				bool active = IsControllerMappingControlActive(i);
				std::string live = GetControllerMappingLiveText(i);
				DrawString(value_end, r.right, y, live,
						active ? TextColour::Green : TextColour::Grey, AlignmentH::Start, false, FontSize::Small);
				y += row_height;
			}
		}

		std::string status = GetControllerConfigStatus();
		if (!status.empty()) {
			DrawString(r.left, r.right, r.bottom - header_height, status,
					TextColour::White, AlignmentH::Start, false, FontSize::Normal);
		}
	}
};

void ShowControllerWindow()
{
	CloseWindowByClass(WindowClass::Controller);
	new ControllerWindow(_controller_desc, 0);
}
