/**
 * Taiga
 * Copyright (C) 2010-2026, Eren Okka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QString>
#include <optional>

#include "base/atf.hpp"
#include "track/episode.hpp"

// Sharing what's playing: Discord Rich Presence and HTTP POST. Ported from v1 `Announcer`.
// Twitter and mIRC aren't ported; the first has no free API and the second no users left.
namespace taiga::sharing {

// Call with the current episode, or `nullopt` when playback stops.
void announce(const std::optional<track::Episode>& episode);

// Removes the Discord status, e.g. when sharing is turned off.
void clear();

// Values for format strings. `urlEncode` is for the HTTP body.
atf::Fields fields(const std::optional<track::Episode>& episode, bool urlEncode);

}  // namespace taiga::sharing
