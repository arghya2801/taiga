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

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <optional>

namespace track {

std::optional<QString> findEpisode(const QString& path, const int anime_id,
                                   const int episode_number);
std::optional<QString> findFolder(const QString& path, const int anime_id);

bool isInsideLibraryFolders(const QString& path);

// Episodes found in the library folders, from the last scan. Not persisted; the scan is cheap
// enough to run on startup.
class AvailableEpisodes final : public QObject {
  Q_OBJECT

public:
  void scan();
  bool isScanning() const;

  int count(const int animeId) const;
  int last(const int animeId) const;
  bool contains(const int animeId, const int episode) const;
  // The next episode after `watched` is on disk.
  bool hasNext(const int animeId, const int watched) const;

signals:
  void scanFinished(int episodeCount);

private:
  QHash<int, QSet<int>> m_episodes;
  bool m_scanning = false;
};

inline AvailableEpisodes availableEpisodes;

}  // namespace track
