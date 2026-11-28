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
#include "gamepad/actions.h"
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
#include "table/sprites.h"
#include "textbuf_gui.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>

#include "safeguards.h"

/**
 * Draw text with inline glyph markers: \@R/\@L/\@U/\@D use OpenTTD's built-in arrow
 * glyphs and \@B uses a small sprite bullet. Raw control codes cannot be embedded in
 * string_view overloads of DrawString, so they are expanded here as separate draws.
 */
static void DrawRichText(int left, int right, int y, std::string_view text, TextColour colour, FontSize fontsize)
{
	int x = left;
	size_t pos = 0;
	while (pos < text.size()) {
		size_t marker = text.find('@', pos);
		if (marker == std::string_view::npos) {
			DrawString(x, right, y, text.substr(pos), colour, AlignmentH::Start, false, fontsize);
			return;
		}
		if (marker > pos) {
			std::string_view part = text.substr(pos, marker - pos);
			DrawString(x, right, y, part, colour, AlignmentH::Start, false, fontsize);
			x += GetStringBoundingBox(part, fontsize).width;
		}
		if (marker + 1 >= text.size()) {
			DrawString(x, right, y, "@", colour, AlignmentH::Start, false, fontsize);
			return;
		}
		switch (text[marker + 1]) {
			case 'R':
				DrawString(x, right, y, STR_JUST_RIGHT_ARROW, colour, AlignmentH::Start, false, fontsize);
				x += GetStringBoundingBox(STR_JUST_RIGHT_ARROW, fontsize).width;
				break;
			case 'L':
				DrawString(x, right, y, STR_JUST_LEFT_ARROW, colour, AlignmentH::Start, false, fontsize);
				x += GetStringBoundingBox(STR_JUST_LEFT_ARROW, fontsize).width;
				break;
			case 'U':
				DrawString(x, right, y, STR_JUST_UP_ARROW, colour, AlignmentH::Start, false, fontsize);
				x += GetStringBoundingBox(STR_JUST_UP_ARROW, fontsize).width;
				break;
			case 'D':
				DrawString(x, right, y, STR_JUST_DOWN_ARROW, colour, AlignmentH::Start, false, fontsize);
				x += GetStringBoundingBox(STR_JUST_DOWN_ARROW, fontsize).width;
				break;
			case 'B': {
				Dimension bullet = GetSpriteSize(SPR_CIRCLE_FOLDED);
				DrawSprite(SPR_CIRCLE_FOLDED, PAL_NONE, x, y + (GetCharacterHeight(fontsize) - bullet.height) / 2);
				x += bullet.width + 3;
				break;
			}
			default:
				DrawString(x, right, y, "@", colour, AlignmentH::Start, false, fontsize);
				x += GetStringBoundingBox("@", fontsize).width;
				break;
		}
		pos = marker + 2;
	}
}

/** Widgets of the controller launcher window. */
enum ControllerLaunchWidgets {
	WID_CL_CAPTION,  ///< Window caption.
	WID_CL_GUIDE,    ///< Open the controller function guide.
	WID_CL_MAPPING,  ///< Open controller selection and mapping.
	WID_CL_OPTIONS,  ///< Open controller options.
	WID_CL_TOUCH,    ///< Open touch settings (Android-only).
	WID_CL_BACKGROUND, ///< Dark window background.
};

static constexpr std::initializer_list<NWidgetPart> _controller_launch_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_CL_CAPTION), SetStringTip(STR_CONTROLLER_LAUNCH_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	/* Like the mapping window, an opaque panel covers the whole window; the
	 * DrawWidget override paints its inside the same dark colour as the canvas. */
	NWidget(WWT_PANEL, Colours::Grey, WID_CL_BACKGROUND),
		NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.frametext),
			NWidget(NWID_VERTICAL), SetPIP(0, 4, 0),
				NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CL_GUIDE), SetMinimalSize(190, 20), SetStringTip(STR_CONTROLLER_GUIDE_BUTTON, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CL_MAPPING), SetMinimalSize(190, 20), SetStringTip(STR_CONTROLLER_MAPPING_BUTTON, STR_NULL),
				NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CL_OPTIONS), SetMinimalSize(190, 20), SetStringTip(STR_CONTROLLER_OPTIONS_BUTTON, STR_NULL),
#ifdef __ANDROID__
				NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_CL_TOUCH), SetMinimalSize(190, 20), SetStringTip(STR_CONTROLLER_TOUCH_BUTTON, STR_NULL),
#endif
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _controller_launch_desc(
	WindowPosition::Center, "controller_launch", 0, 0,
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
				/* A gamepad A press can land on this widget when the crosshair is parked
				 * over it; that must never cancel an active mapping flow. */
				if (IsGamepadVirtualClick()) break;
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

		/* Three columns sized as fractions of the widget: fixed offsets clip text on
		 * high-DPI screens. Labels and text are kept short so Normal font fits. */
		const int header_height = GetCharacterHeight(FontSize::Normal);
		const int row_height = GetCharacterHeight(FontSize::Normal);
		const int list_width = r.right - r.left;
		const int label_end = r.left + list_width * 26 / 100;
		const int value_end = r.left + list_width * 62 / 100;
		int y = r.top;

		DrawString(r.left, label_end, y, "Control", TextColour::Silver, AlignmentH::Start, false, FontSize::Normal);
		DrawString(label_end, value_end, y, "Mapping", TextColour::Silver, AlignmentH::Start, false, FontSize::Normal);
		DrawString(value_end, r.right, y, "Live", TextColour::Silver, AlignmentH::Start, false, FontSize::Normal);
		y += header_height;

		std::string prompt = GetControllerMappingPrompt();
		if (!prompt.empty()) {
			DrawRichText(r.left, r.right, y, prompt, TextColour::Yellow, FontSize::Normal);
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

		/* Rows are always drawn; unmapped steps simply show an em dash. */
		for (size_t i = 0; i < rows.size(); i++) {
			if (y + row_height > r.bottom) break;
			TextColour colour = rows[i].second == "—"
				? TextColour::Grey
				: (IsControllerMapping() && i == current ? TextColour::Yellow : TextColour::White);
			DrawRichText(r.left, label_end, y, rows[i].first, colour, FontSize::Normal);
			DrawString(label_end, value_end, y, rows[i].second, colour, AlignmentH::Start, false, FontSize::Normal);
			bool active = IsControllerMappingControlActive(i);
			std::string live = GetControllerMappingLiveText(i);
			DrawString(value_end, r.right, y, live,
					active ? TextColour::Green : TextColour::Grey, AlignmentH::Start, false, FontSize::Normal);
			y += row_height;
		}

		std::string status = GetControllerConfigStatus();
		if (!status.empty()) {
			DrawString(r.left, r.right, r.bottom - header_height, status,
					TextColour::White, AlignmentH::Start, false, FontSize::Normal);
		}
	}
};

/** Widgets of the controller guide window. */
enum ControllerGuideWidgets {
	WID_CG_CAPTION, ///< Window caption.
	WID_CG_CANVAS,  ///< Plain-text function list.
	WID_CG_BACKGROUND, ///< Dark window background.
};

static constexpr std::initializer_list<NWidgetPart> _controller_guide_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_CG_CAPTION), SetStringTip(STR_CONTROLLER_GUIDE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_CG_BACKGROUND),
		NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.frametext),
			NWidget(WWT_EMPTY, Colours::Invalid, WID_CG_CANVAS), SetMinimalSize(480, 340), SetFill(1, 1),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _controller_guide_desc(
	WindowPosition::Automatic, "controller_guide", 0, 0,
	WindowClass::ControllerGuide, WindowClass::None,
	{},
	_controller_guide_widgets
);

/** Launcher: choose the function guide or the mapping window. */
void ShowControllerMappingWindow();
void ShowControllerOptionsWindow();

struct ControllerLaunchWindow : Window {
	ControllerLaunchWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		/* Paint the inside of the background panel like the mapping window's canvas. */
		if (widget == WID_CL_BACKGROUND) GfxFillRect(r.Shrink(1), PC_BLACK);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget == WID_CL_GUIDE || widget == WID_CL_MAPPING || widget == WID_CL_OPTIONS
#ifdef __ANDROID__
			 || widget == WID_CL_TOUCH
#endif
) {
			this->Close();
			if (widget == WID_CL_GUIDE) {
				ShowControllerGuideWindow();
			} else if (widget == WID_CL_MAPPING) {
				ShowControllerMappingWindow();
			}
#ifdef __ANDROID__
			else if (widget == WID_CL_TOUCH) {
				ShowTouchSettingsWindow();
			}
#endif
			else {
				ShowControllerOptionsWindow();
			}
		}
	}
};

/** Hardcoded controller function guide: one plain-text line per button. */
struct ControllerGuideWindow : Window {
	ControllerGuideWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget == WID_CG_BACKGROUND) {
			GfxFillRect(r.Shrink(1), PC_BLACK);
			return;
		}
		if (widget != WID_CG_CANVAS) return;
		GfxFillRect(r, PC_BLACK);

		static constexpr const char * const _lines[] = {
			"A @R Select / Interact",
			"@B Open a toolbar widget",
			"@B Place materials",
			"@B Interact with objects",
			"B @R Close Widget",
			"@B Close the currently focused widget",
			"X @R Inspect / Tooltip",
			"@B Temporarily hide tooltip",
			"@B Inspect depot vehicle",
			"Y @R Cancel",
			"@B Cancel a drag@Rbuild event",
			"@B Close tooltip",
			"L/R Shoulder @R Ctrl/Shift Modifiers",
			"@B L+A to share orders between vehicles",
			"@B R+A to estimate build cost",
			"L3 @R Pause or resume the game",
			"R3 @R Toggle fast forward",
			"L Stick @R Crosshair",
			"@B Moves the mouse",
			"R Stick @R Pan",
			"@B Moves the map / main viewport",
			"L/R Trigger @R Zoom",
			"@B Zooms the map / main viewport",
			"@B Hold down to zoom automatically",
		};

		const int row_height = GetCharacterHeight(FontSize::Normal) + 4;
		int y = r.top + 4;
		for (const char *line : _lines) {
			DrawRichText(r.left, r.right, y, line,
					line[0] == '@' ? TextColour::Silver : TextColour::White,
					FontSize::Normal);
			y += row_height;
		}
	}
};

/** Widgets of the controller options window. */
enum ControllerOptionsWidgets {
	WID_CO_CAPTION,   ///< Window caption.
	WID_CO_BACKGROUND,///< Dark window background.
	WID_CO_CANVAS,    ///< Option rows.
};

static constexpr std::initializer_list<NWidgetPart> _controller_options_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_CO_CAPTION), SetStringTip(STR_CONTROLLER_OPTIONS_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(WWT_PANEL, Colours::Grey, WID_CO_BACKGROUND),
		NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.frametext),
			NWidget(WWT_EMPTY, Colours::Invalid, WID_CO_CANVAS), SetMinimalSize(520, 260), SetFill(1, 1),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _controller_options_desc(
	WindowPosition::Automatic, "controller_options", 0, 0,
	WindowClass::ControllerOptions, WindowClass::None,
	{},
	_controller_options_widgets
);

/** Option rows shown in the controller options window. */
enum ControllerOptionRow : uint {
	CO_SWAP_STICKS,
	CO_INVERT_LX,
	CO_INVERT_LY,
	CO_INVERT_RX,
	CO_INVERT_RY,
	CO_INVERT_LT,
	CO_INVERT_RT,
	CO_DEADZONE_LS,
	CO_DEADZONE_RS,
	CO_DEADZONE_LT,
	CO_DEADZONE_RT,
	CO_SPEED_LS,
	CO_SPEED_RS,
	CO_COUNT,
};

static const char *GetControllerOptionLabel(uint row)
{
	switch (row) {
		case CO_SWAP_STICKS: return "Swap sticks";
		case CO_INVERT_LX:   return "L Stick X Invert";
		case CO_INVERT_LY:   return "L Stick Y Invert";
		case CO_INVERT_RX:   return "R Stick X Invert";
		case CO_INVERT_RY:   return "R Stick Y Invert";
		case CO_INVERT_LT:   return "L Trigger Invert";
		case CO_INVERT_RT:   return "R Trigger Invert";
		case CO_DEADZONE_LS: return "L Stick Deadzone";
		case CO_DEADZONE_RS: return "R Stick Deadzone";
		case CO_DEADZONE_LT: return "L Trigger Deadzone";
		case CO_DEADZONE_RT: return "R Trigger Deadzone";
		case CO_SPEED_LS:    return "L Stick Speed";
		case CO_SPEED_RS:    return "R Stick Speed";
		default:             return "";
	}
}

static std::string GetControllerOptionValue(uint row)
{
	const ControllerOptions &opt = GetControllerOptions();
	switch (row) {
		case CO_SWAP_STICKS: return opt.swap_sticks ? "On" : "Off";
		case CO_INVERT_LX:   return opt.invert_left_x ? "On" : "Off";
		case CO_INVERT_LY:   return opt.invert_left_y ? "On" : "Off";
		case CO_INVERT_RX:   return opt.invert_right_x ? "On" : "Off";
		case CO_INVERT_RY:   return opt.invert_right_y ? "On" : "Off";
		case CO_INVERT_LT:   return opt.invert_left_trigger ? "On" : "Off";
		case CO_INVERT_RT:   return opt.invert_right_trigger ? "On" : "Off";
		case CO_DEADZONE_LS: return fmt::format("{}%", opt.deadzone_left_stick);
		case CO_DEADZONE_RS: return fmt::format("{}%", opt.deadzone_right_stick);
		case CO_DEADZONE_LT: return fmt::format("{}%", opt.deadzone_left_trigger);
		case CO_DEADZONE_RT: return fmt::format("{}%", opt.deadzone_right_trigger);
		case CO_SPEED_LS:
		case CO_SPEED_RS: {
			ControllerAxisSpeed speed = row == CO_SPEED_LS ? opt.left_stick_speed : opt.right_stick_speed;
			switch (speed) {
				case ControllerAxisSpeed::Slow:   return "Slow";
				case ControllerAxisSpeed::Normal: return "Normal";
				case ControllerAxisSpeed::Fast:   return "Fast";
			}
			return "Normal";
		}
		default: return "";
	}
}

/** Controller options window. Option rows are custom-drawn and gamepad-focusable. */
struct ControllerOptionsWindow : Window {
	uint row_height = 0;       ///< Height of one option row.
	uint pending_option = 0;   ///< Row currently being edited by a query/dropdown.

	ControllerOptionsWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->row_height = GetCharacterHeight(FontSize::Normal) + 8;
		this->InitNested(number);
	}

	uint GetGamepadListRowCount(WidgetID widget) const override
	{
		return widget == WID_CO_CANVAS ? static_cast<uint>(CO_COUNT) : 0;
	}

	Rect GetGamepadListRowRect(WidgetID widget, uint row) const override
	{
		Rect r = this->GetWidget<NWidgetBase>(widget)->GetCurrentRect();
		int top = r.top + 2 + static_cast<int>(row) * static_cast<int>(this->row_height);
		return {r.left, top, r.right, top + static_cast<int>(this->row_height)};
	}

	void OnGamepadListSelect(WidgetID widget, uint row) override
	{
		if (widget == WID_CO_CANVAS && row < CO_COUNT) this->ActivateOption(row);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget == WID_CO_BACKGROUND) GfxFillRect(r.Shrink(1), PC_BLACK);
		if (widget != WID_CO_CANVAS) return;

		int y = r.top + 2;
		for (uint row = 0; row < CO_COUNT; row++) {
			DrawString(r.left + 6, r.right - 170, y, GetControllerOptionLabel(row), TextColour::White, AlignmentH::Start, false, FontSize::Normal);
			DrawString(r.right - 165, r.right - 6, y, GetControllerOptionValue(row), TextColour::Silver, AlignmentH::Start, false, FontSize::Normal);
			y += static_cast<int>(this->row_height);
		}
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget != WID_CO_CANVAS) return;
		Rect r = this->GetWidget<NWidgetBase>(widget)->GetCurrentRect();
		if (pt.y <= r.top + 2) return;
		uint row = static_cast<uint>((pt.y - r.top - 2) / static_cast<int>(this->row_height));
		if (row < CO_COUNT) this->ActivateOption(row);
	}

	void OnQueryTextFinished(std::optional<std::string> str) override
	{
		if (!str.has_value()) return;
		int value = 0;
		auto result = std::from_chars(str->data(), str->data() + str->size(), value);
		if (result.ec != std::errc{}) return;
		value = std::clamp(value, 0, 100);

		ControllerOptions &opt = GetControllerOptions();
		switch (this->pending_option) {
			case CO_DEADZONE_LS: opt.deadzone_left_stick = static_cast<uint8_t>(value); break;
			case CO_DEADZONE_RS: opt.deadzone_right_stick = static_cast<uint8_t>(value); break;
			case CO_DEADZONE_LT: opt.deadzone_left_trigger = static_cast<uint8_t>(value); break;
			case CO_DEADZONE_RT: opt.deadzone_right_trigger = static_cast<uint8_t>(value); break;
			default: return;
		}
		SaveControllerConfig();
		this->SetDirty();
	}

	void OnDropdownSelect(WidgetID widget, int index, int) override
	{
		if (widget != WID_CO_CANVAS || index < 0 || index > 2) return;
		ControllerAxisSpeed speed = static_cast<ControllerAxisSpeed>(index);
		ControllerOptions &opt = GetControllerOptions();
		if (this->pending_option == CO_SPEED_LS) {
			opt.left_stick_speed = speed;
		} else if (this->pending_option == CO_SPEED_RS) {
			opt.right_stick_speed = speed;
		} else {
			return;
		}
		SaveControllerConfig();
		this->SetDirty();
	}

private:
	void ActivateOption(uint row)
	{
		ControllerOptions &opt = GetControllerOptions();
		switch (row) {
			case CO_SWAP_STICKS:      opt.swap_sticks = !opt.swap_sticks; break;
			case CO_INVERT_LX:        opt.invert_left_x = !opt.invert_left_x; break;
			case CO_INVERT_LY:        opt.invert_left_y = !opt.invert_left_y; break;
			case CO_INVERT_RX:        opt.invert_right_x = !opt.invert_right_x; break;
			case CO_INVERT_RY:        opt.invert_right_y = !opt.invert_right_y; break;
			case CO_INVERT_LT:        opt.invert_left_trigger = !opt.invert_left_trigger; break;
			case CO_INVERT_RT:        opt.invert_right_trigger = !opt.invert_right_trigger; break;
			case CO_DEADZONE_LS:
			case CO_DEADZONE_RS:
			case CO_DEADZONE_LT:
			case CO_DEADZONE_RT:
				this->pending_option = row;
				ShowQueryString(GetControllerOptionValue(row), STR_CONFIG_SETTING_QUERY_CAPTION, 4, this, CS_NUMERAL, {});
				return;
			case CO_SPEED_LS:
			case CO_SPEED_RS: {
				this->pending_option = row;
				DropDownList list;
				list.push_back(MakeDropDownListStringItem("Slow", 0));
				list.push_back(MakeDropDownListStringItem("Normal", 1));
				list.push_back(MakeDropDownListStringItem("Fast", 2));
				int selected = static_cast<int>(row == CO_SPEED_LS ? opt.left_stick_speed : opt.right_stick_speed);
				Rect canvas = this->GetWidget<NWidgetBase>(WID_CO_CANVAS)->GetCurrentRect();
				int y = canvas.top + 2 + static_cast<int>(row) * static_cast<int>(this->row_height);
				Rect anchor{canvas.right - 165, y, canvas.right - 6, y + static_cast<int>(this->row_height) - 1};
				ShowDropDownListAt(this, std::move(list), selected, WID_CO_CANVAS, anchor, Colours::Grey);
				return;
			}
			default: return;
		}
		SaveControllerConfig();
		this->SetDirty();
	}
};

void ShowControllerLaunchWindow()
{
	CloseWindowByClass(WindowClass::Controller);
	CloseWindowByClass(WindowClass::ControllerGuide);
	CloseWindowByClass(WindowClass::ControllerOptions);
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

void ShowControllerOptionsWindow()
{
	CloseWindowByClass(WindowClass::ControllerOptions);
	new ControllerOptionsWindow(_controller_options_desc, 0);
}

void ShowControllerWindow()
{
	ShowControllerLaunchWindow();
}
