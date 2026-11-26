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

#include <cmath>
#include <string>

#include "safeguards.h"

/** Widgets of the controller launcher window. */
enum ControllerLaunchWidgets {
	WID_CL_CAPTION,  ///< Window caption.
	WID_CL_GUIDE,    ///< Open the controller function guide.
	WID_CL_MAPPING,  ///< Open controller selection and mapping.
};

static constexpr std::initializer_list<NWidgetPart> _controller_launch_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_CL_CAPTION), SetStringTip(STR_CONTROLLER_LAUNCH_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.frametext),
		NWidget(NWID_HORIZONTAL), SetPIP(0, 8, 0),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CL_GUIDE), SetMinimalSize(170, 20), SetStringTip(STR_CONTROLLER_GUIDE_BUTTON, STR_NULL),
			NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CL_MAPPING), SetMinimalSize(190, 20), SetStringTip(STR_CONTROLLER_MAPPING_BUTTON, STR_NULL),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _controller_launch_desc(
	WindowPosition::Automatic, "controller_launch", 0, 0,
	WindowClass::Controller, WindowClass::None,
	{},
	_controller_launch_widgets
);

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
			NWidget(WWT_EMPTY, Colours::Invalid, WID_CT_LIST), SetMinimalSize(640, 300), SetFill(1, 1),
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

		/* Four columns sized as fractions of the widget: fixed offsets clip text on
		 * high-DPI screens. Labels and text are kept short so Normal font fits. */
		const int header_height = GetCharacterHeight(FontSize::Normal);
		const int row_height = GetCharacterHeight(FontSize::Normal);
		const int list_width = r.right - r.left;
		const int label_end = r.left + list_width * 26 / 100;
		const int value_end = r.left + list_width * 50 / 100;
		const int live_end = r.left + list_width * 65 / 100;
		int y = r.top;

		DrawString(r.left, r.right, y, "Control / Mapping / Live / In-game function", TextColour::Silver, AlignmentH::Start, false, FontSize::Normal);
		y += header_height;

		std::string prompt = GetControllerMappingPrompt();
		if (!prompt.empty()) {
			DrawString(r.left, r.right, y, prompt, TextColour::Yellow, AlignmentH::Start, false, FontSize::Normal);
			y += header_height;
		}

		auto rows = GetControllerMappingRows();
		size_t current = rows.size();
		for (size_t i = 0; i < rows.size(); i++) {
			if (rows[i].second == "—") {
				current = i; /* the step currently being captured */
				break;
			}
		}

		/* The function descriptions are part of the window even before mapping, so the
		 * rows are always drawn; unmapped steps simply show an em dash. */
		for (size_t i = 0; i < rows.size(); i++) {
			if (y + row_height > r.bottom) break;
			TextColour colour = rows[i].second == "—"
				? TextColour::Grey
				: (IsControllerMapping() && i == current ? TextColour::Yellow : TextColour::White);
			DrawString(r.left, label_end, y, rows[i].first, colour, AlignmentH::Start, false, FontSize::Normal);
			DrawString(label_end, value_end, y, rows[i].second, colour, AlignmentH::Start, false, FontSize::Normal);
			bool active = IsControllerMappingControlActive(i);
			std::string live = GetControllerMappingLiveText(i);
			DrawString(value_end, live_end, y, live,
					active ? TextColour::Green : TextColour::Grey, AlignmentH::Start, false, FontSize::Normal);
			DrawString(live_end, r.right, y, GetControllerMappingDescription(i),
					rows[i].second == "—" ? TextColour::Grey : TextColour::Silver,
					AlignmentH::Start, false, FontSize::Normal);
			y += row_height;
		}

		std::string status = GetControllerConfigStatus();
		if (!status.empty()) {
			DrawString(r.left, r.right, r.bottom - header_height, status,
					TextColour::White, AlignmentH::Start, false, FontSize::Normal);
		}
	}
};

/** Draw a small filled circle with a handful of horizontal spans. */
static void DrawFilledCircle(int cx, int cy, int radius, PixelColour colour)
{
	for (int dy = -radius; dy <= radius; dy++) {
		int half = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - dy * dy)));
		GfxFillRect(Rect{cx - half, cy + dy, cx + half, cy + dy}, colour);
	}
}

/** Widgets of the controller guide window. */
enum ControllerGuideWidgets {
	WID_CG_CAPTION, ///< Window caption.
	WID_CG_CANVAS,  ///< Schematic controller and its function descriptions.
};

static constexpr std::initializer_list<NWidgetPart> _controller_guide_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_CG_CAPTION), SetStringTip(STR_CONTROLLER_GUIDE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.frametext),
		NWidget(WWT_EMPTY, Colours::Invalid, WID_CG_CANVAS), SetMinimalSize(640, 360), SetFill(1, 1),
	EndContainer(),
};

static WindowDesc _controller_guide_desc(
	WindowPosition::Automatic, "controller_guide", 0, 0,
	WindowClass::ControllerGuide, WindowClass::None,
	{},
	_controller_guide_widgets
);

/** Launcher: choose the function guide or the mapping window. */
void ShowControllerGuideWindow();
void ShowControllerMappingWindow();

struct ControllerLaunchWindow : Window {
	ControllerLaunchWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget == WID_CL_GUIDE || widget == WID_CL_MAPPING) {
			this->Close();
			if (widget == WID_CL_GUIDE) ShowControllerGuideWindow(); else ShowControllerMappingWindow();
		}
	}
};

/**
 * Hardcoded controller function guide. The schematic is drawn with primitives from the
 * CC0 Xbox controller reference (Wikimedia Commons, File:Xbox_Controller.svg): offset
 * sticks, D-pad left, face buttons right, shoulders on top. One connector line per row.
 */
struct ControllerGuideWindow : Window {
	ControllerGuideWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_CG_CANVAS) return;
		GfxFillRect(r, PC_BLACK);

		static constexpr const char * const _lines[] = {
			"A - Left click / hold-drag",
			"B - Close focused widget (C3)",
			"X - Right click",
			"Y - Esc: cancel tool",
			"L/R Shoulder - Ctrl / Shift",
			"L3+R3 - Pause / resume",
			"L Stick - crosshair (C2)",
			"R Stick - pan (C2 placeholder)",
			"L/R Trigger - zoom (C2)",
		};
		struct GuideLink { int x; int y; int row; };
		static constexpr GuideLink _links[] = {
			{205, 130, 0}, {240, 105, 1}, {170, 105, 2}, {205, 80, 3}, {115, 20, 4},
			{190, 165, 5}, {110, 115, 6}, {190, 165, 7}, {135, 10, 8},
		};

		const int bx = r.left + 20;
		const int by = r.top + 45;
		const int row_height = GetCharacterHeight(FontSize::Normal) + 8;
		const int text_x = r.left + 335;

		/* Controller silhouette: body, grips, shoulders. */
		GfxFillRect(Rect{bx + 30, by + 35, bx + 270, by + 195}, PC_GREY);
		GfxFillRect(Rect{bx, by + 70, bx + 70, by + 195}, PC_GREY);
		GfxFillRect(Rect{bx + 230, by + 70, bx + 300, by + 195}, PC_GREY);
		GfxFillRect(Rect{bx + 55, by + 10, bx + 115, by + 32}, PC_DARK_GREY);
		GfxFillRect(Rect{bx + 175, by + 10, bx + 235, by + 32}, PC_DARK_GREY);

		/* D-pad cross and the two offset sticks. */
		GfxFillRect(Rect{bx + 55, by + 100, bx + 75, by + 140}, PC_DARK_GREY);
		GfxFillRect(Rect{bx + 40, by + 115, bx + 90, by + 125}, PC_DARK_GREY);
		DrawFilledCircle(bx + 110, by + 115, 22, PC_DARK_GREY);
		DrawFilledCircle(bx + 190, by + 165, 22, PC_DARK_GREY);

		/* Face buttons: Y top, X left, B right, A bottom. */
		DrawFilledCircle(bx + 205, by + 80, 11, PC_YELLOW);
		DrawFilledCircle(bx + 170, by + 105, 11, PC_LIGHT_BLUE);
		DrawFilledCircle(bx + 240, by + 105, 11, PC_RED);
		DrawFilledCircle(bx + 205, by + 130, 11, PC_GREEN);
		DrawString(bx + 201, bx + 212, by + 74, "Y", TextColour::Black, AlignmentH::Start, false, FontSize::Normal);
		DrawString(bx + 166, bx + 177, by + 99, "X", TextColour::Black, AlignmentH::Start, false, FontSize::Normal);
		DrawString(bx + 236, bx + 247, by + 99, "B", TextColour::Black, AlignmentH::Start, false, FontSize::Normal);
		DrawString(bx + 201, bx + 212, by + 124, "A", TextColour::Black, AlignmentH::Start, false, FontSize::Normal);

		for (size_t i = 0; i < std::size(_links); i++) {
			const GuideLink &link = _links[i];
			GfxDrawLine(bx + link.x, by + link.y, text_x - 6, r.top + 20 + link.row * row_height + row_height / 2, PC_WHITE);
			DrawString(text_x, r.right, r.top + 20 + link.row * row_height, _lines[link.row],
					TextColour::White, AlignmentH::Start, false, FontSize::Normal);
		}
	}
};

void ShowControllerLaunchWindow()
{
	CloseWindowByClass(WindowClass::Controller);
	CloseWindowByClass(WindowClass::ControllerGuide);
	new ControllerLaunchWindow(_controller_launch_desc, 0);
}

void ShowControllerMappingWindow()
{
	CloseWindowByClass(WindowClass::Controller);
	new ControllerWindow(_controller_desc, 0);
}

void ShowControllerGuideWindow()
{
	CloseWindowByClass(WindowClass::ControllerGuide);
	new ControllerGuideWindow(_controller_guide_desc, 0);
}

void ShowControllerWindow()
{
	ShowControllerLaunchWindow();
}
