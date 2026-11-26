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
#include "actions.h"
#include "../dropdown_func.h"
#include "../window_gui.h"
#include "../widget_type.h"
#include "../gfx_func.h"

#include <vector>

#include "../safeguards.h"

static Window *_gamepad_focus_window = nullptr;
static WidgetID _gamepad_focus_widget = -1;

/** Candidate used by one D-pad movement. Rectangles are in screen coordinates. */
struct FocusCandidate {
	Window *w;
	WidgetID widget;
	Rect rect;
};

/** Check whether a widget should participate in focus navigation. */
static bool IsFocusableWidget(const NWidgetCore *nw)
{
	if (nw == nullptr || nw->IsDisabled() || nw->GetIndex() < 0) return false;

	Rect r = nw->GetCurrentRect();
	if (r.Width() <= 0 || r.Height() <= 0) return false;

	switch (nw->type & WWT_MASK) {
		case WWT_PUSHTXTBTN:
		case WWT_TEXTBTN:
		case WWT_PUSHIMGBTN:
		case WWT_IMGBTN:
		case WWT_BOOLBTN:
		case WWT_IMGTEXTBTN:
		case WWT_DROPDOWN:
		case WWT_EDITBOX:
		case WWT_CLOSEBOX:
		case WWT_PANEL:
		case WWT_INSET:
			return true;
		default:
			return false;
	}
}

static void MarkFocusDirty()
{
	if (_gamepad_focus_window == nullptr || _gamepad_focus_widget < 0) return;
	if (_gamepad_focus_window->window_class == WindowClass::DropdownMenu) {
		_gamepad_focus_window->SetDirty();
	} else {
		_gamepad_focus_window->SetWidgetDirty(_gamepad_focus_widget);
	}
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

/** Collect candidates from one window. Dropdown items are individual candidates. */
static std::vector<FocusCandidate> CollectWindowCandidates(Window *w)
{
	std::vector<FocusCandidate> candidates;

	if (w->window_class == WindowClass::DropdownMenu) {
		int count = GetDropdownFocusItemCount(w);
		for (int i = 0; i < count; i++) {
			candidates.push_back({w, i, GetDropdownFocusItemRect(w, i)});
		}
		return candidates;
	}

	for (const auto &pair : w->widget_lookup) {
		NWidgetCore *nw = dynamic_cast<NWidgetCore *>(pair.second);
		if (!IsFocusableWidget(nw)) continue;

		Rect local = nw->GetCurrentRect();
		int cx = (local.left + local.right) / 2;
		int cy = (local.top + local.bottom) / 2;
		if (GetWidgetFromPos(w, cx, cy) != nw->GetIndex()) continue;

		candidates.push_back({w, nw->GetIndex(), local.Translate(w->left, w->top)});
	}
	return candidates;
}

static Window *FrontWindowWithCandidates()
{
	for (Window *w : Window::IterateFromFront()) {
		if (!CollectWindowCandidates(w).empty()) return w;
	}
	return nullptr;
}

/** Focus navigation is confined to the active window until B closes it. */
static Window *GetActiveFocusWindow()
{
	if (FocusWindowAlive(_gamepad_focus_window)) return _gamepad_focus_window;
	return FrontWindowWithCandidates();
}

static const FocusCandidate *FirstCandidate(const std::vector<FocusCandidate> &candidates)
{
	const FocusCandidate *best = nullptr;
	int best_score = INT32_MAX;
	for (const FocusCandidate &candidate : candidates) {
		int score = candidate.rect.top * 10000 + candidate.rect.left;
		if (score < best_score) {
			best_score = score;
			best = &candidate;
		}
	}
	return best;
}

/** Perpendicular separation between two rectangles; zero when they overlap. */
static int PerpendicularGap(const Rect &a, const Rect &b, bool horizontal)
{
	if (horizontal) {
		if (a.top <= b.bottom && b.top <= a.bottom) return 0;
		return a.top < b.top ? b.top - a.bottom : a.top - b.bottom;
	}
	if (a.left <= b.right && b.left <= a.right) return 0;
	return a.left < b.left ? b.left - a.right : a.left - b.right;
}

/** Score one candidate relative to the current rectangle using rendered edges. */
static int DirectionScore(const Rect &current, const FocusCandidate &candidate, GamepadFocusDirection direction)
{
	const Rect &next = candidate.rect;
	switch (direction) {
		case GamepadFocusDirection::Up:
			if (next.bottom > current.top) return INT32_MAX;
			return current.top - next.bottom + PerpendicularGap(current, next, false) * 4;
		case GamepadFocusDirection::Down:
			if (next.top < current.bottom) return INT32_MAX;
			return next.top - current.bottom + PerpendicularGap(current, next, false) * 4;
		case GamepadFocusDirection::Left:
			if (next.right > current.left) return INT32_MAX;
			return current.left - next.right + PerpendicularGap(current, next, true) * 4;
		case GamepadFocusDirection::Right:
			if (next.left < current.right) return INT32_MAX;
			return next.left - current.right + PerpendicularGap(current, next, true) * 4;
	}
	return INT32_MAX;
}

/** Fallback score when there is no candidate in the pressed direction. */
static int WrapScore(const FocusCandidate &candidate, GamepadFocusDirection direction)
{
	switch (direction) {
		case GamepadFocusDirection::Up:    return -candidate.rect.bottom;
		case GamepadFocusDirection::Down:  return  candidate.rect.top;
		case GamepadFocusDirection::Left:  return -candidate.rect.right;
		case GamepadFocusDirection::Right: return  candidate.rect.left;
	}
	return INT32_MAX;
}

static Rect GetFocusRect(bool &valid)
{
	valid = false;
	if (!IsGamepadFocusActive()) return {};

	if (_gamepad_focus_window->window_class == WindowClass::DropdownMenu) {
		Rect r = GetDropdownFocusItemRect(_gamepad_focus_window, _gamepad_focus_widget);
		valid = r.Width() > 0 && r.Height() > 0;
		return r;
	}

	NWidgetCore *nw = _gamepad_focus_window->GetWidget<NWidgetCore>(_gamepad_focus_widget);
	if (nw == nullptr || !IsFocusableWidget(nw)) return {};
	Rect local = nw->GetCurrentRect();
	int cx = (local.left + local.right) / 2;
	int cy = (local.top + local.bottom) / 2;
	if (GetWidgetFromPos(_gamepad_focus_window, cx, cy) != _gamepad_focus_widget) return {};
	valid = true;
	return local.Translate(_gamepad_focus_window->left, _gamepad_focus_window->top);
}

static void SnapCursorToFocus()
{
	bool valid = false;
	Rect r = GetFocusRect(valid);
	if (!valid) return;
	SetGamepadCursorPosition((r.left + r.right) / 2, (r.top + r.bottom) / 2);
}

static void SetFocus(const FocusCandidate &candidate)
{
	MarkFocusDirty();
	_gamepad_focus_window = candidate.w;
	_gamepad_focus_widget = candidate.widget;
	MarkFocusDirty();
	SetFocusedWindow(candidate.w);
	SnapCursorToFocus();
}

void MoveGamepadFocus(GamepadFocusDirection direction)
{
	Window *active = GetActiveFocusWindow();
	if (active == nullptr) return;

	std::vector<FocusCandidate> candidates = CollectWindowCandidates(active);
	if (candidates.empty()) return;

	Rect current{};
	bool have_current = false;
	bool valid = false;
	current = GetFocusRect(valid);
	have_current = valid;

	const FocusCandidate *best = nullptr;
	int best_score = INT32_MAX;
	for (const FocusCandidate &candidate : candidates) {
		if (have_current && candidate.widget == _gamepad_focus_widget) continue;
		int score = have_current ? DirectionScore(current, candidate, direction) : INT32_MAX;
		if (score < best_score) {
			best_score = score;
			best = &candidate;
		}
	}

	if (best == nullptr || best_score == INT32_MAX) {
		for (const FocusCandidate &candidate : candidates) {
			if (have_current && candidate.w == _gamepad_focus_window && candidate.widget == _gamepad_focus_widget) continue;
			int score = WrapScore(candidate, direction);
			if (score < best_score) {
				best_score = score;
				best = &candidate;
			}
		}
	}
	if (best == nullptr && !have_current) best = FirstCandidate(candidates);
	if (best == nullptr) return;

	SetFocus(*best);
}

void ActivateGamepadFocus()
{
	Window *w = nullptr;
	WidgetID widget = -1;
	if (!GetGamepadFocus(w, widget)) return;

	if (w->window_class == WindowClass::DropdownMenu) {
		ActivateDropdownFocusItem(w, widget);
		return;
	}

	NWidgetCore *nw = w->GetWidget<NWidgetCore>(widget);
	if (nw == nullptr) return;

	Rect local = nw->GetCurrentRect();
	if ((nw->type & WWT_MASK) == WWT_EDITBOX) {
		w->SetFocusedWidget(widget);
		SetFocusedWindow(w);
	} else {
		w->OnClick({(local.left + local.right) / 2, (local.top + local.bottom) / 2}, widget, 1);
	}
}

/** After activation, move focus to a newly opened window instead of staying on the button. */
void UpdateGamepadFocusAfterActivation()
{
	Window *front = FrontWindowWithCandidates();
	if (front == nullptr) {
		ClearGamepadFocus();
		return;
	}
	if (front == _gamepad_focus_window && _gamepad_focus_widget >= 0) return;

	auto candidates = CollectWindowCandidates(front);
	const FocusCandidate *first = FirstCandidate(candidates);
	if (first != nullptr) SetFocus(*first);
}
