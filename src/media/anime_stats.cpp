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

#include "anime_stats.hpp"

#include <algorithm>
#include <cmath>

#include "media/anime_utils.hpp"

namespace anime {

ListStats calculateListStats(const QList<ListEntry>& entries,
                             const std::function<const Details*(int)>& findItem) {
  using list::Status;

  ListStats stats;
  int scored = 0;
  double scoreSum = 0.0;

  for (const auto& entry : entries) {
    if (entry.pending_delete || entry.status == Status::NotInList) continue;
    const auto* item = findItem(entry.anime_id);
    if (!item) continue;

    ++stats.animeCount;

    const int length = estimateEpisodeLength(*item) * 60;
    const int watched = entry.watched_episodes + entry.rewatched_times * item->episode_count;
    stats.episodesWatched += watched;
    stats.secondsWatched += static_cast<qint64>(length) * watched;

    if (entry.status != Status::Completed && entry.status != Status::Dropped) {
      const int remaining = estimateEpisodeCount(*item, entry.watched_episodes) -
                            entry.watched_episodes;
      stats.secondsPlanned += static_cast<qint64>(length) * std::max(remaining, 0);
    }

    if (entry.score > 0) {
      ++scored;
      scoreSum += entry.score;
      const int bucket = std::clamp(static_cast<int>(std::lround(entry.score / 10.0)), 1, 10);
      ++stats.scoreCounts[bucket - 1];
    }
  }

  if (scored > 0) {
    stats.meanScore = scoreSum / scored;
    double squares = 0.0;
    for (const auto& entry : entries) {
      if (entry.pending_delete || entry.status == Status::NotInList || entry.score <= 0) continue;
      if (!findItem(entry.anime_id)) continue;
      squares += std::pow(entry.score - stats.meanScore, 2.0);
    }
    stats.scoreDeviation = std::sqrt(squares / scored);
  }

  return stats;
}

}  // namespace anime
