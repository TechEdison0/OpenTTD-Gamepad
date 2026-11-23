/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file gamepad_debug.cpp On-screen controller diagnostics (chunk C0 scaffolding). */

#include "stdafx.h"

#include "gamepad_debug.h"
#include "window_gui.h"
#include "window_func.h"
#include "gfx_func.h"

#include <deque>
#include <mutex>

#include "safeguards.h"

/** Number of event lines kept for display. */
static const size_t GAMEPAD_DEBUG_HISTORY = 64;

/** Recent controller messages, oldest first. Guarded by \c _gamepad_debug_mutex. */
static std::mutex _gamepad_debug_mutex;
static std::deque<std::string> _gamepad_debug_lines;

void GamepadDebugRecord(std::string_view text)
{
	std::lock_guard<std::mutex> lock(_gamepad_debug_mutex);
	_gamepad_debug_lines.emplace_back(text);
	while (_gamepad_debug_lines.size() > GAMEPAD_DEBUG_HISTORY) _gamepad_debug_lines.pop_front();
}

/** Widgets of the controller diagnostics window. */
enum GamepadDebugWidgets {
	WID_GPD_CAPTION, ///< Window caption.
	WID_GPD_CANVAS,  ///< Area the event lines are drawn into.
};

static constexpr std::initializer_list<NWidgetPart> _gamepad_debug_widgets = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, Colours::Grey),
		NWidget(WWT_CAPTION, Colours::Grey, WID_GPD_CAPTION), SetTextStyle(TextColour::White),
		NWidget(WWT_STICKYBOX, Colours::Grey),
	EndContainer(),
	NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.frametext),
		NWidget(WWT_EMPTY, Colours::Invalid, WID_GPD_CANVAS), SetMinimalSize(320, 200), SetFill(1, 1),
	EndContainer(),
};

/** Window description of the controller diagnostics window. */
static WindowDesc _gamepad_debug_desc(
	WindowPosition::Automatic, "gamepad_debug", 0, 0,
	WindowClass::GamepadDebug, WindowClass::None,
	{},
	_gamepad_debug_widgets
);

/**
 * Window showing which controllers SDL sees and what they do.
 */
struct GamepadDebugWindow : Window {
private:
	uint accumulated_ms = 0; ///< Time since the last repaint.

public:
	GamepadDebugWindow(WindowDesc &desc, WindowNumber number) : Window(desc)
	{
		this->InitNested(number);
		this->SetDirty();
	}

	void OnRealtimeTick(uint delta_ms) override
	{
		this->accumulated_ms += delta_ms;
		if (this->accumulated_ms >= 250) {
			this->accumulated_ms = 0;
			this->SetDirty();
		}
	}

	void DrawWidget(const Rect &r, WidgetID widget) const override
	{
		if (widget != WID_GPD_CANVAS) return;

		const int line_height = GetCharacterHeight(FontSize::Small);
		int y = r.top;

		/* Custom widgets do not get a background painted for us; clear it ourselves or
		 * successive frames smear into each other. */
		GfxFillRect(r, PC_BLACK);

		DrawString(r.left, r.right, y, "Controller diagnostics", TextColour::White, AlignmentH::Start, false, FontSize::Normal);
		y += GetCharacterHeight(FontSize::Normal);

		std::lock_guard<std::mutex> lock(_gamepad_debug_mutex);
		for (auto it = _gamepad_debug_lines.rbegin(); it != _gamepad_debug_lines.rend(); ++it) {
			if (y + line_height > r.bottom) break;
			DrawString(r.left, r.right, y, *it, TextColour::White, AlignmentH::Start, false, FontSize::Small);
			y += line_height;
		}
	}
};

void ShowGamepadDebugWindow()
{
	AllocateWindowDescFront<GamepadDebugWindow>(_gamepad_debug_desc, 0);
}
