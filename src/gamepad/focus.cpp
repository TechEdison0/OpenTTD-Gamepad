/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD.
 * If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file focus.cpp C3 gamepad widget focus model. */

#include "../stdafx.h"
#include "focus.h"
#include "../window_gui.h"
#include "../widget_type.h"
#include "../gfx_func.h"

#include <vector>

#include "../safeguards.h"

static Window *_gamepad_focus_window = nullptr;
static WidgetID _gamepad_focus_widget = -1;

/** Candidate used by one D-pad movement. */
struct FocusCandidate {
	Window *w;
	WidgetID widget;
	Rect rect;
};

/** Check whether a widget should participate in focus navigation. */
static bool IsFocusableWidget(const NWidgetCore *nw)
{
	if (nw == nullptr || nw->IsDisabled()) return false;

	Rect r = nw->GetCurrentRect();
	if (r.Width() <= 0 || r.Height() <= 0) return false;

	switch (nw->type & WWT_MASK) {
		case WWT_PUSHTXTBTN:
		case WWT_TEXTBTN:
		case WWT_PUSHBTN:
		case WWT_IMGBTN:
		case WWT_BOOLBTN:
		case WWT_IMGTEXTBTN:
		case WWT_DROPDOWN:
		case WWT_EDITBOX:
		case WWT_CLOSEBOX:
			return true;
		default:
			return false;
	}
}

static void MarkFocusDirty()
{
	if (_gamepad_focus_window == nullptr || _gamepad_focus_widget < 0) return;
	_gamepad_focus_window->SetWidgetDirty(_gamepad_focus_widget);
}

static bool FocusWindowAlive(Window *w)
{
	if (w == nullptr) return false;
	for (Window *candidate : Window::IterateFromFront()) {
		if (candidate == w) return true;
	}
	return false;
}

bool IsGamepadFocusActive()
{
	if (!FocusWindowAlive(_gamepad_focus_window)) {
		_gamepad_focus_window = nullptr;
		_gamepad_focus_widget = -1;
		return false;
	}
	return _gamepad_focus_widget >= 0;
}

bool GetGamepadFocus(Window *&w, WidgetID &widget)
{
	if (!IsGamepadFocusActive()) return false;
	w = _gamepad_focus_window;
	widget = _gamepad_focus_widget;
	return true;
}

void ClearGamepadFocus()
{
	MarkFocusDirty();
	_gamepad_focus_window = nullptr;
	_gamepad_focus_widget = -1;
}

/** Collect focusable widgets from the front-most window that has any. */
static std::vector<FocusCandidate> CollectCandidates(Window *&w)
{
	std::vector<FocusCandidate> candidates;
	for (Window *candidate_window : Window::IterateFromFront()) {
		for (const auto &pair : candidate_window->widget_lookup) {
			NWidgetCore *nw = dynamic_cast<NWidgetCore *>(pair.second);
			if (!IsFocusableWidget(nw)) continue;

			candidates.push_back({candidate_window, nw->GetIndex(), nw->GetCurrentRect()});
		}
		if (!candidates.empty()) {
			w = candidate_window;
			return candidates;
		}
	}
	return candidates;
}

/** Score one candidate relative to the current focus. Lower is better. */
static int DirectionScore(const FocusCandidate &candidate, int cx, int cy, GamepadFocusDirection direction)
{
	int dx = (candidate.rect.left + candidate.rect.right) / 2 - cx;
	int dy = (candidate.rect.top + candidate.rect.bottom) / 2 - cy;

	switch (direction) {
		case GamepadFocusDirection::Up:     if (dy >= 0) return INT32_MAX; return -dy * 100 + std::abs(dx) * 10;
		case GamepadFocusDirection::Down:   if (dy <= 0) return INT32_MAX; return  dy * 100 + std::abs(dx) * 10;
		case GamepadFocusDirection::Left:   if (dx >= 0) return INT32_MAX; return -dx * 100 + std::abs(dy) * 10;
		case GamepadFocusDirection::Right:  if (dx <= 0) return INT32_MAX; return  dx * 100 + std::abs(dy) * 10;
	}
	return INT32_MAX;
}

void MoveGamepadFocus(GamepadFocusDirection direction)
{
	Window *active_window = nullptr;
	std::vector<FocusCandidate> candidates = CollectCandidates(active_window);
	if (candidates.empty()) return;

	if (_gamepad_focus_window != nullptr && _gamepad_focus_window != active_window) ClearGamepadFocus();

	int cx = 0;
	int cy = 0;
	bool have_current = false;
	if (_gamepad_focus_window == active_window && _gamepad_focus_widget >= 0) {
		NWidgetCore *nw = active_window->GetWidget<NWidgetCore>(_gamepad_focus_widget);
		if (IsFocusableWidget(nw)) {
			Rect r = nw->GetCurrentRect();
			cx = (r.left + r.right) / 2;
			cy = (r.top + r.bottom) / 2;
			have_current = true;
		}
	}

	const FocusCandidate *best = nullptr;
	int best_score = INT32_MAX;
	for (const FocusCandidate &candidate : candidates) {
		int score = have_current
			? DirectionScore(candidate, cx, cy, direction)
			: candidate.rect.top * 1000 + candidate.rect.left;
		if (score < best_score) {
			best_score = score;
			best = &candidate;
		}
	}
	if (best == nullptr || best_score == INT32_MAX) return;

	MarkFocusDirty();
	_gamepad_focus_window = best->w;
	_gamepad_focus_widget = best->widget;
	MarkFocusDirty();
	SetFocusedWindow(best->w);
}

void ActivateGamepadFocus()
{
	Window *w = nullptr;
	WidgetID widget = -1;
	if (!GetGamepadFocus(w, widget)) return;

	w->OnHotkey(widget);
	SetFocusedWindow(w);
}
