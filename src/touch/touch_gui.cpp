/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file touch_gui.cpp Touch settings window (chunk T1). */

#include "../stdafx.h"
#include "touch_gui.h"
#include "touch.h"
#include "../window_gui.h"
#include "../window_func.h"
#include "../gfx_func.h"
#include "../palette_func.h"
#include "../strings_func.h"
#include "../table/strings.h"
#include "../safeguards.h"

/** Widgets of the touch settings window. */
enum TouchWidgets {
	WID_TW_CAPTION,  ///< Window caption.
	WID_TW_MODE,     ///< Touch mode toggle button.
	WID_TW_BACKGROUND, ///< Dark window background.
};

static constexpr std::initializer_list<NWidgetPart> _touch_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_TW_CAPTION), SetStringTip(STR_TOUCH_SETTINGS_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	/* Background panel */
	NWidget(WWT_PANEL, Colours::Grey, WID_TW_BACKGROUND),
		NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.frametext),
		NWidget(NWID_VERTICAL), SetPIP(0, 4, 0),
		NWidget(WWT_PUSHTXTBTN, Colours::Grey, WID_TW_MODE), SetMinimalSize(190, 20), SetStringTip(STR_TOUCH_MODE_TOOLTIP, STR_NULL),
		EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _touch_desc(
	WindowPosition::Center, "touch_settings", 0, 0,
	WindowClass::TouchSettings, WindowClass::None,
	{},
	_touch_widgets
);

struct TouchSettingsWindow : Window {
	TouchSettingsWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
	}

	void OnClick([[maybe_unused]] Point pt, WidgetID widget, [[maybe_unused]] int click_count) override
	{
		if (widget == WID_TW_MODE) {
			TouchMode current = TouchGetMode();
			TouchMode next = (current == TouchMode::Trackpad) ? TouchMode::Absolute : TouchMode::Trackpad;
			TouchSetMode(next);
			TouchSaveMode();
			this->SetDirty();
		}
	}

	std::string GetWidgetString(WidgetID widget, StringID stringid) const override
	{
		if (widget == WID_TW_MODE) {
			TouchMode mode = TouchGetMode();
			if (mode == TouchMode::Trackpad) {
				return std::string{GetString(STR_TOUCH_MODE_TRACKPAD)};
			} else {
				return std::string{GetString(STR_TOUCH_MODE_ABSOLUTE)};
			}
		}
		return Window::GetWidgetString(widget, stringid);
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget == WID_TW_BACKGROUND) GfxFillRect(r.Shrink(1), PC_BLACK);
	}
};

void ShowTouchSettingsWindow()
{
	CloseWindowByClass(WindowClass::TouchSettings);
	new TouchSettingsWindow(_touch_desc, 0);
}