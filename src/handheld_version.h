/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of
 * the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file handheld_version.h Handheld build label for user-facing captions. */

#ifndef HANDHELD_VERSION_H
#define HANDHELD_VERSION_H

#include <string_view>

/**
 * User-facing build label.
 * Stable releases use "1.0"; pre-release builds override with the release tag via
 * -DOPENTTD_HANDHELD_BUILD_LABEL="v20261128-t0-touch-architecture" (or similar).
 */
#ifndef OPENTTD_HANDHELD_BUILD_LABEL
#define OPENTTD_HANDHELD_BUILD_LABEL "1.0"
#endif

inline constexpr std::string_view HANDHELD_BUILD_LABEL = OPENTTD_HANDHELD_BUILD_LABEL;

#endif /* HANDHELD_VERSION_H */