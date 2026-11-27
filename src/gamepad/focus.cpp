/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, version 2.
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
static int _gamepad_focus_row = -1;
static Window *_last_focus_window = nullptr;
static WidgetID _last_focus_widget = -1;

/** Candidate used by one D-pad movement. Rectangles are in screen coordinates. */
struct FocusCandidate {
	Window *w;
	WidgetID widget;
	Rect rect;
	int list_row = -1; ///< Row index for virtual list candidates, otherwise -1.
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
		case WWT_IMGBTN_2:
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

/** Windows that must never receive D-pad focus. */
static bool IsExcludedFromGamepadFocus(const Window *w)
{
	/* The in-game bottom bar is WindowClass::Statusbar, not MainWindow. */
	return w->window_class == WindowClass::Statusbar;
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

/** Suppress the main-menu root while a dialog with its own widgets is open. */
static bool IsWindowSuppressed(const Window *w)
{
	if (IsExcludedFromGamepadFocus(w)) return true;
	if (w->window_class != WindowClass::SelectGame) return false;

	for (Window *other : Window::IterateFromFront()) {
		if (other == w || IsExcludedFromGamepadFocus(other)) continue;
		if (other->window_class == WindowClass::ToolTips || other->window_class == WindowClass::MainWindow) continue;
		for (const auto &pair : other->widget_lookup) {
			NWidgetCore *nw = dynamic_cast<NWidgetCore *>(pair.second);
			if (IsFocusableWidget(nw)) return true;
		}
	}
	return false;
}

static void AddCandidate(std::vector<FocusCandidate> &candidates, Window *w, WidgetID widget, Rect rect, int list_row = -1)
{
	if (rect.Width() <= 0 || rect.Height() <= 0) return;
	candidates.push_back({w, widget, rect, list_row});
}

/** Collect candidates from one window. Dropdown items are individual candidates. */
static std::vector<FocusCandidate> CollectWindowCandidates(Window *w)
{
	std::vector<FocusCandidate> candidates;
	if (IsWindowSuppressed(w)) return candidates;

	if (w->window_class == WindowClass::DropdownMenu) {
		int count = GetDropdownFocusItemCount(w);
		for (int i = 0; i < count; i++) {
			AddCandidate(candidates, w, i, GetDropdownFocusItemRect(w, i));
		}
		return candidates;
	}

	for (const auto &pair : w->widget_lookup) {
		NWidgetCore *nw = dynamic_cast<NWidgetCore *>(pair.second);
		if (nw == nullptr || nw->GetIndex() < 0) continue;

		bool focusable = IsFocusableWidget(nw);
		if (!focusable && (nw->type & WWT_MASK) == WWT_PANEL) {
			/* Custom-drawn push panels (station/depot direction buttons) are leaf panels. */
			NWidgetContainer *container = dynamic_cast<NWidgetContainer *>(nw);
			focusable = container != nullptr && container->IsEmpty();
		}
		if (!focusable) {
			int min_value, max_value, value;
			if (w->GetGamepadSliderInfo(nw->GetIndex(), min_value, max_value, value)) focusable = true;
		}

		uint rows = w->GetGamepadListRowCount(nw->GetIndex());
		if (rows > 0) {
			for (uint row = 0; row < rows; row++) {
				Rect local = w->GetGamepadListRowRect(nw->GetIndex(), row);
				AddCandidate(candidates, w, nw->GetIndex(), local.Translate(w->left, w->top), static_cast<int>(row));
			}
			continue;
		}
		if (!focusable) continue;

		Rect local = nw->GetCurrentRect();
		int cx = (local.left + local.right) / 2;
		int cy = (local.top + local.bottom) / 2;
		if (GetWidgetFromPos(w, cx, cy) != nw->GetIndex()) continue;

		AddCandidate(candidates, w, nw->GetIndex(), local.Translate(w->left, w->top));
	}
	return candidates;
}

static std::vector<FocusCandidate> CollectAllCandidates()
{
	std::vector<FocusCandidate> candidates;
	for (Window *w : Window::IterateFromFront()) {
		auto window_candidates = CollectWindowCandidates(w);
		candidates.insert(candidates.end(), window_candidates.begin(), window_candidates.end());
	}
	return candidates;
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
	/* Dropdown rows are hit-tested from the real cursor position. Keep navigation
	 * decoupled from the cursor; ActivateDropdownFocusItem() parks it on activation. */
	if (_gamepad_focus_window->window_class == WindowClass::DropdownMenu) return;
	SetGamepadCursorPosition((r.left + r.right) / 2, (r.top + r.bottom) / 2);
}

static void SetFocus(const FocusCandidate &candidate)
{
	MarkFocusDirty();
	_gamepad_focus_window = candidate.w;
	_gamepad_focus_widget = candidate.widget;
	_gamepad_focus_row = candidate.list_row;
	_last_focus_window = candidate.w;
	_last_focus_widget = candidate.widget;
	MarkFocusDirty();

	if (candidate.w->window_class == WindowClass::DropdownMenu) SetDropdownDragMode(candidate.w, false);

	if (candidate.list_row >= 0) candidate.w->OnGamepadListFocus(candidate.widget, candidate.list_row);

	SetFocusedWindow(candidate.w);
	SnapCursorToFocus();
}

static bool FindLastFocusCandidate(FocusCandidate &out)
{
	Window *w = _last_focus_window;
	if (w == nullptr || !FocusWindowAlive(w) || IsExcludedFromGamepadFocus(w)) return false;

	if (w->window_class == WindowClass::DropdownMenu) {
		Rect r = GetDropdownFocusItemRect(w, _last_focus_widget);
		if (r.Width() <= 0 || r.Height() <= 0) return false;
		out = {w, _last_focus_widget, r};
		return true;
	}

	NWidgetCore *nw = w->GetWidget<NWidgetCore>(_last_focus_widget);
	if (nw == nullptr || !IsFocusableWidget(nw)) return false;
	Rect local = nw->GetCurrentRect();
	int cx = (local.left + local.right) / 2;
	int cy = (local.top + local.bottom) / 2;
	if (GetWidgetFromPos(w, cx, cy) != _last_focus_widget) return false;
	out = {w, _last_focus_widget, local.Translate(w->left, w->top)};
	return true;
}

void MoveGamepadFocus(GamepadFocusDirection direction)
{
	if (_gamepad_focus_window == nullptr || !FocusWindowAlive(_gamepad_focus_window)) {
		SaveGamepadCursorPosition();
		FocusCandidate last{};
		if (FindLastFocusCandidate(last)) SetFocus(last);
	}

	std::vector<FocusCandidate> candidates = CollectAllCandidates();
	if (candidates.empty()) return;

	Rect current{};
	bool have_current = false;
	bool valid = false;
	current = GetFocusRect(valid);
	have_current = valid;

	const FocusCandidate *best = nullptr;
	int best_score = INT32_MAX;
	for (const FocusCandidate &candidate : candidates) {
		if (have_current && candidate.w == _gamepad_focus_window && candidate.widget == _gamepad_focus_widget) continue;
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
	if (_gamepad_focus_row >= 0) {
		w->OnGamepadListSelect(widget, static_cast<uint>(_gamepad_focus_row));
		return;
	}
	if (nw == nullptr) return;

	Rect local = nw->GetCurrentRect();
	if ((nw->type & WWT_MASK) == WWT_EDITBOX) {
		w->SetFocusedWidget(widget);
		SetFocusedWindow(w);
	} else {
		w->OnClick({(local.left + local.right) / 2, (local.top + local.bottom) / 2}, widget, 1);
	}
}
