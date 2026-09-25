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

#include "scanner.hpp"

#include <QDirIterator>
#include <QtConcurrentRun>
#include <algorithm>
#include <optional>
#include <ranges>
#include <vector>

#include "media/anime.hpp"
#include "media/anime_db.hpp"
#include "taiga/settings.hpp"
#include "track/episode.hpp"
#include "track/recognition.hpp"

namespace track {

static bool containsEpisodeNumber(const Episode& episode, const int episode_number) {
  const auto numbers = episode.elements(anitomy::ElementKind::Episode);
  if (numbers.empty()) return false;

  const auto toInt = [](const std::string& value) { return QString::fromStdString(value).toInt(); };
  const auto [low, high] = std::ranges::minmax(numbers | std::views::transform(toInt));

  return low <= episode_number && episode_number <= high;
}

std::optional<QString> findEpisode(const QString& path, const int anime_id,
                                   const int episode_number) {
  QDirIterator it{path, QDir::Files, QDirIterator::Subdirectories};

  while (it.hasNext()) {
    const auto info = it.nextFileInfo();

    if (!info.isFile()) continue;

    auto episode = recognition::parseFileInfo(info);

    if (!recognition::isVideoFile(episode)) continue;

    if (!containsEpisodeNumber(episode, episode_number)) continue;

    if (track::recognition::identify(episode) != anime_id) continue;

    return info.filePath();
  }

  return std::nullopt;
}

std::optional<QString> findFolder(const QString& path, const int anime_id) {
  QDirIterator it{path, QDir::Dirs, QDirIterator::Subdirectories};

  while (it.hasNext()) {
    const auto info = it.nextFileInfo();

    if (!info.isDir()) continue;

    auto episode = recognition::parseFileInfo(info);

    if (track::recognition::identify(episode) != anime_id) continue;

    return info.filePath();
  }

  return std::nullopt;
}

void AvailableEpisodes::scan() {
  if (m_scanning) return;
  m_scanning = true;

  // Walking and parsing happen off the UI thread. `identify` reads the anime database, which
  // isn't thread-safe, so it runs on the UI thread once the walk is done.
  QtConcurrent::run([folders = taiga::settings.libraryFolders()] {
    std::vector<Episode> episodes;
    for (const auto& folder : folders) {
      QDirIterator it{QString::fromStdString(folder), QDir::Files, QDirIterator::Subdirectories};
      while (it.hasNext()) {
        auto episode = recognition::parseFileInfo(it.nextFileInfo());
        if (recognition::isVideoFile(episode)) episodes.push_back(std::move(episode));
      }
    }
    return episodes;
  }).then(this, [this](std::vector<Episode> episodes) {
    m_episodes.clear();
    int count = 0;
    for (auto& episode : episodes) {
      const int id = recognition::identify(episode);
      if (id == anime::kUnknownId) continue;
      auto range = episode.episodeNumberRange();
      if (!range) {
        // Movies and other single-episode anime usually have no number.
        const auto item = anime::db.item(id);
        if (!item || item->episode_count != 1) continue;
        range = std::pair{1, 1};
      }
      for (int number = range->first; number <= range->second; ++number) {
        m_episodes[id].insert(number);
        ++count;
      }
    }
    m_scanning = false;
    emit scanFinished(count);
  });
}

bool AvailableEpisodes::isScanning() const {
  return m_scanning;
}

int AvailableEpisodes::count(const int animeId) const {
  return m_episodes.value(animeId).size();
}

int AvailableEpisodes::last(const int animeId) const {
  const auto episodes = m_episodes.value(animeId);
  return episodes.isEmpty() ? 0 : *std::ranges::max_element(episodes);
}

bool AvailableEpisodes::contains(const int animeId, const int episode) const {
  return m_episodes.value(animeId).contains(episode);
}

bool AvailableEpisodes::hasNext(const int animeId, const int watched) const {
  return contains(animeId, watched + 1);
}

bool isInsideLibraryFolders(const QString& path) {
  const auto normalizedPath = QDir::cleanPath(path);

  return std::ranges::any_of(taiga::settings.libraryFolders(), [&](const std::string& folder) {
    auto root = QDir::cleanPath(QString::fromStdString(folder));
    if (!root.endsWith(u'/')) root += u'/';
    return normalizedPath.startsWith(root, Qt::CaseInsensitive);
  });
}

}  // namespace track
