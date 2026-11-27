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

#include <algorithm>
#include <vector>

#include "../safeguards.h"

static Window *_gamepad_focus_window = nullptr;
static WidgetID _gamepad_focus_widget = -1;
static Window *_last_focus_window = nullptr;
static WidgetID _last_focus_widget = -1;

struct FocusEntry {
	Window *window;
	WidgetID widget;

	bool operator!=(const FocusEntry &other) const { return window != other.window || widget != other.widget; }
};
static std::vector<FocusEntry> _focus_stack;
static bool _gamepad_dropdown_selection_pending = false;
static std::vector<Window *> _windows_before_activation;

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
	if (_gamepad_focus_window != nullptr && FocusWindowAlive(_gamepad_focus_window) &&
			!IsExcludedFromGamepadFocus(_gamepad_focus_window)) {
		FocusEntry entry{_gamepad_focus_window, _gamepad_focus_widget};
		if (_focus_stack.empty() || _focus_stack.back() != entry) _focus_stack.push_back(entry);
	}

	MarkFocusDirty();
	_gamepad_focus_window = nullptr;
	_gamepad_focus_widget = -1;
	_gamepad_dropdown_selection_pending = false;
}

/** Collect candidates from one window. Dropdown items are individual candidates. */
static std::vector<FocusCandidate> CollectWindowCandidates(Window *w)
{
	std::vector<FocusCandidate> candidates;
	if (IsExcludedFromGamepadFocus(w)) return candidates;

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

/** Find the front-most eligible window above a known window, excluding it. */
static Window *FrontWindowAfter(Window *skip)
{
	for (Window *w : Window::IterateFromFront()) {
		if (w == skip) break;
		if (IsExcludedFromGamepadFocus(w)) continue;
		if (!CollectWindowCandidates(w).empty()) return w;
	}
	return nullptr;
}

static std::vector<Window *> SnapshotWindows()
{
	std::vector<Window *> snapshot;
	for (Window *w : Window::IterateFromFront()) snapshot.push_back(w);
	return snapshot;
}

static Window *FindNewEligibleWindow(const std::vector<Window *> &before)
{
	for (Window *w : Window::IterateFromFront()) {
		if (std::find(before.begin(), before.end(), w) != before.end()) continue;
		if (IsExcludedFromGamepadFocus(w)) continue;
		if (!CollectWindowCandidates(w).empty()) return w;
	}
	return nullptr;
}

static bool BuildFocusCandidate(Window *w, WidgetID widget, FocusCandidate &out)
{
	if (w == nullptr || !FocusWindowAlive(w) || IsExcludedFromGamepadFocus(w)) return false;

	if (w->window_class == WindowClass::DropdownMenu) {
		Rect r = GetDropdownFocusItemRect(w, widget);
		if (r.Width() <= 0 || r.Height() <= 0) return false;
		out = {w, widget, r};
		return true;
	}

	NWidgetCore *nw = w->GetWidget<NWidgetCore>(widget);
	if (nw == nullptr || !IsFocusableWidget(nw)) return false;
	Rect local = nw->GetCurrentRect();
	int cx = (local.left + local.right) / 2;
	int cy = (local.top + local.bottom) / 2;
	if (GetWidgetFromPos(w, cx, cy) != widget) return false;
	out = {w, widget, local.Translate(w->left, w->top)};
	return true;
}

static bool FindLastFocusCandidate(FocusCandidate &out)
{
	return BuildFocusCandidate(_last_focus_window, _last_focus_widget, out);
}

static void PruneFocusStack()
{
	std::erase_if(_focus_stack, [](const FocusEntry &entry) {
		return !FocusWindowAlive(entry.window) || IsExcludedFromGamepadFocus(entry.window);
	});
}

/** Focus navigation is confined to the active window until B closes it. */
static void SetFocus(const FocusCandidate &candidate);

static Window *GetActiveFocusWindow()
{
	if (FocusWindowAlive(_gamepad_focus_window) &&
			!IsExcludedFromGamepadFocus(_gamepad_focus_window)) {
		return _gamepad_focus_window;
	}

	PruneFocusStack();
	if (!_focus_stack.empty()) {
		FocusCandidate candidate{};
		if (BuildFocusCandidate(_focus_stack.back().window, _focus_stack.back().widget, candidate)) {
			SetFocus(candidate);
			return _gamepad_focus_window;
		}
	}

	ClearGamepadFocus();
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
	/* Dropdown rows are hit-tested from the real cursor position. Keep navigation
	 * decoupled from the cursor; ActivateDropdownFocusItem() parks it on activation. */
	if (_gamepad_focus_window->window_class == WindowClass::DropdownMenu) return;
	SetGamepadCursorPosition((r.left + r.right) / 2, (r.top + r.bottom) / 2);
}

static void SetFocus(const FocusCandidate &candidate)
{
	if (_gamepad_focus_window != nullptr && _gamepad_focus_window != candidate.w &&
			FocusWindowAlive(_gamepad_focus_window) &&
			!IsExcludedFromGamepadFocus(_gamepad_focus_window)) {
		FocusEntry entry{_gamepad_focus_window, _gamepad_focus_widget};
		if (_focus_stack.empty() || _focus_stack.back() != entry) _focus_stack.push_back(entry);
	}

	MarkFocusDirty();
	_gamepad_focus_window = candidate.w;
	_gamepad_focus_widget = candidate.widget;
	_last_focus_window = candidate.w;
	_last_focus_widget = candidate.widget;
	MarkFocusDirty();

	if (candidate.w->window_class == WindowClass::DropdownMenu) {
		_gamepad_dropdown_selection_pending = true;
		SetDropdownDragMode(candidate.w, false);
	} else {
		_gamepad_dropdown_selection_pending = false;
	}

	SetFocusedWindow(candidate.w);
	SnapCursorToFocus();
}

void MoveGamepadFocus(GamepadFocusDirection direction)
{
	if (_gamepad_focus_window == nullptr || !FocusWindowAlive(_gamepad_focus_window)) {
		SaveGamepadCursorPosition();
		PruneFocusStack();
		if (!_focus_stack.empty()) {
			FocusCandidate candidate{};
			if (BuildFocusCandidate(_focus_stack.back().window, _focus_stack.back().widget, candidate)) SetFocus(candidate);
		} else {
			FocusCandidate last{};
			if (FindLastFocusCandidate(last)) SetFocus(last);
		}
	}

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

Window *ActivateGamepadFocus()
{
	_windows_before_activation = SnapshotWindows();

	Window *w = nullptr;
	WidgetID widget = -1;
	if (!GetGamepadFocus(w, widget)) {
		_windows_before_activation.clear();
		return nullptr;
	}

	if (w->window_class == WindowClass::DropdownMenu) {
		_windows_before_activation.clear();
		ActivateDropdownFocusItem(w, widget);
		return w;
	}

	NWidgetCore *nw = w->GetWidget<NWidgetCore>(widget);
	if (nw == nullptr) return nullptr;

	Rect local = nw->GetCurrentRect();
	if ((nw->type & WWT_MASK) == WWT_EDITBOX) {
		w->SetFocusedWidget(widget);
		SetFocusedWindow(w);
	} else {
		w->OnClick({(local.left + local.right) / 2, (local.top + local.bottom) / 2}, widget, 1);
	}
	return w;
}

/** After activation, move focus to a newly opened window instead of staying on the button. */
void UpdateGamepadFocusAfterActivation(Window *activated)
{
	if (activated == nullptr) return;
	if (activated->window_class == WindowClass::DropdownMenu) return;

	Window *target = FindNewEligibleWindow(_windows_before_activation);
	_windows_before_activation.clear();
	if (!FocusWindowAlive(activated)) {
		if (target == nullptr) {
			PruneFocusStack();
			if (!_focus_stack.empty()) {
				FocusCandidate candidate{};
				if (BuildFocusCandidate(_focus_stack.back().window, _focus_stack.back().widget, candidate)) {
					SetFocus(candidate);
					return;
				}
			}
			target = FrontWindowWithCandidates();
		}
	} else if (target == nullptr) {
		target = FrontWindowAfter(activated);
		if (target == nullptr) target = activated;
	}

	if (target == nullptr) {
		ClearGamepadFocus();
		return;
	}
	if (target == _gamepad_focus_window && _gamepad_focus_widget >= 0) return;

	auto candidates = CollectWindowCandidates(target);
	const FocusCandidate *first = FirstCandidate(candidates);
	if (first != nullptr) SetFocus(*first);
}

void FocusGamepadWindow(Window *w)
{
	if (w == nullptr || IsExcludedFromGamepadFocus(w)) return;

	auto candidates = CollectWindowCandidates(w);
	const FocusCandidate *first = FirstCandidate(candidates);
	if (first != nullptr) SetFocus(*first);
}

void FocusPreviousGamepadWindow()
{
	PruneFocusStack();
	if (_focus_stack.empty()) {
		ClearGamepadFocus();
		return;
	}

	FocusEntry entry = _focus_stack.back();
	_focus_stack.pop_back();

	FocusCandidate candidate{};
	if (BuildFocusCandidate(entry.window, entry.widget, candidate)) {
		SetFocus(candidate);
	} else {
		ClearGamepadFocus();
	}
}

void NotifyGamepadDropdownSelectionCompleted(Window *dropdown_parent, WidgetID dropdown_button,
		bool dropdown_closed, Window *opened_window)
{
	if (!dropdown_closed || !_gamepad_dropdown_selection_pending) return;
	_gamepad_dropdown_selection_pending = false;

	/* Prefer the exact window created by OnDropdownSelect. Toolbar-aligned windows
	 * sit below the main toolbar in z-order, so z-order scanning cannot find them. */
	Window *target = nullptr;
	if (opened_window != nullptr && !IsExcludedFromGamepadFocus(opened_window) &&
			!CollectWindowCandidates(opened_window).empty()) {
		target = opened_window;
	}
	if (target == nullptr) target = FrontWindowAfter(dropdown_parent);
	if (target == nullptr) target = dropdown_parent;
	if (target == nullptr) return;

	auto candidates = CollectWindowCandidates(target);
	const FocusCandidate *first = nullptr;
	if (target == dropdown_parent) {
		for (const FocusCandidate &candidate : candidates) {
			if (candidate.widget == dropdown_button) {
				first = &candidate;
				break;
			}
		}
	}
	if (first == nullptr) first = FirstCandidate(candidates);
	if (first != nullptr) SetFocus(*first);
}
