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

#include "anime_list_model.hpp"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QPalette>
#include <QSize>
#include <QTimer>

#include "base/string.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/image_provider.hpp"
#include "gui/utils/rating.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list_utils.hpp"
#include "media/anime_season.hpp"
#include "media/anime_utils.hpp"
#include "taiga/options.hpp"
#include "track/scanner.hpp"

namespace gui {

AnimeListModel::AnimeListModel(QObject* parent) : QAbstractListModel(parent) {
  m_ids = anime::db.items().keys();
  rebuildRows();

  connect(&imageProvider, &ImageProvider::posterChanged, this, [this](int id) {
    if (const auto row = m_rows.value(id, -1); row > -1) {
      emit dataChanged(index(row), index(row), {static_cast<int>(AnimeListItemDataRole::Poster)});
    }
  });

  connect(&track::availableEpisodes, &track::AvailableEpisodes::scanFinished, this, [this] {
    if (!m_ids.isEmpty()) emit dataChanged(index(0, 0), index(m_ids.size() - 1, NUM_COLUMNS - 1));
  });

  connect(&anime::db, &anime::Database::itemUpdated, this, &AnimeListModel::refreshRow);
  connect(&anime::db, &anime::Database::itemDeleted, this, &AnimeListModel::deleteRow);
  connect(&anime::db, &anime::Database::entryUpdated, this, &AnimeListModel::refreshRow);
  connect(&anime::db, &anime::Database::entryDeleted, this, &AnimeListModel::refreshRow);
}

// Sync emits one signal per item. Coalescing them into a single insert and a single
// `dataChanged` keeps the proxy model from re-sorting once per item.
void AnimeListModel::refreshRow(int id) {
  if (m_pending.isEmpty()) QTimer::singleShot(0, this, &AnimeListModel::flushPending);
  m_pending.insert(id);
}

void AnimeListModel::flushPending() {
  QList<int> newIds;
  int first = m_ids.size();
  int last = -1;
  for (const int id : std::as_const(m_pending)) {
    if (const auto row = m_rows.value(id, -1); row > -1) {
      first = std::min(first, row);
      last = std::max(last, row);
    } else if (anime::db.item(id)) {
      newIds.append(id);
    }
  }
  m_pending.clear();

  if (last > -1) emit dataChanged(index(first, 0), index(last, NUM_COLUMNS - 1));
  addIds(newIds);
}

void AnimeListModel::deleteRow(int id) {
  m_pending.remove(id);
  if (const auto row = m_rows.value(id, -1); row > -1) {
    beginRemoveRows({}, row, row);
    m_ids.removeAt(row);
    rebuildRows();
    endRemoveRows();
  }
}

void AnimeListModel::addIds(const QList<int>& ids) {
  QList<int> newIds;
  for (const int id : ids) {
    if (!m_rows.contains(id)) {
      m_rows.insert(id, m_ids.size() + newIds.size());
      newIds.append(id);
    }
  }
  if (newIds.isEmpty()) return;

  const int first = m_ids.size();
  beginInsertRows({}, first, first + newIds.size() - 1);
  m_ids.append(newIds);
  endInsertRows();
}

void AnimeListModel::rebuildRows() {
  m_rows.clear();
  m_rows.reserve(m_ids.size());
  for (int row = 0; row < m_ids.size(); ++row) m_rows.insert(m_ids.at(row), row);
}

int AnimeListModel::rowCount(const QModelIndex&) const {
  return m_ids.size();
}

int AnimeListModel::columnCount(const QModelIndex&) const {
  return NUM_COLUMNS;
}

QVariant AnimeListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return {};

  const auto anime = getAnime(index);
  if (!anime) return {};

  const auto entry = getListEntry(index);

  switch (role) {
    case Qt::DisplayRole:
      switch (index.column()) {
        case COLUMN_TITLE:
          return QString::fromStdString(anime::preferredTitle(*anime));
        case COLUMN_DURATION:
          return formatEpisodeLength(anime->episode_length);
        case COLUMN_REWATCHES:
          if (entry) return entry->rewatched_times;
          break;
        case COLUMN_SCORE:
          if (entry) return formatRating(entry->score);
          break;
        case COLUMN_AVERAGE:
          return formatScore(anime->score);
        case COLUMN_TYPE:
          return formatType(anime->type);
        case COLUMN_SEASON:
          return formatSeason(anime::Season(anime->date_started));
        case COLUMN_STARTED:
          if (entry) return formatFuzzyDate(entry->date_started);
          break;
        case COLUMN_COMPLETED:
          if (entry) return formatFuzzyDate(entry->date_completed);
          break;
        case COLUMN_LAST_UPDATED:
          if (entry) return formatAsRelativeTime(entry->last_updated, "-");
          break;
        case COLUMN_NOTES:
          if (entry) return QString::fromStdString(entry->notes);
          break;
      }
      break;

    case Qt::FontRole:
      if (index.column() == COLUMN_TITLE && hasNewEpisode(*anime, entry)) {
        QFont font;
        font.setBold(true);
        return font;
      }
      break;

    case Qt::ToolTipRole:
      switch (index.column()) {
        case COLUMN_TITLE:
          return QString::fromStdString(anime::preferredTitle(*anime));
        case COLUMN_AIRING:
          return formatStatus(anime::airingStatus(*anime));
        case COLUMN_SEASON:
          return formatFuzzyDate(anime->date_started);
        case COLUMN_LAST_UPDATED:
          if (entry) return formatTimestamp(entry->last_updated);
          break;
        case COLUMN_NOTES:
          if (entry) return QString::fromStdString(entry->notes);
          break;
      }
      break;

    case Qt::TextAlignmentRole: {
      switch (index.column()) {
        case COLUMN_PROGRESS:
        case COLUMN_REWATCHES:
        case COLUMN_SCORE:
        case COLUMN_AVERAGE:
        case COLUMN_TYPE:
          return QVariant(Qt::AlignHCenter | Qt::AlignVCenter);
        case COLUMN_DURATION:
        case COLUMN_SEASON:
        case COLUMN_STARTED:
        case COLUMN_COMPLETED:
        case COLUMN_LAST_UPDATED:
          return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        default:
          return {};
      }
      break;
    }

    case Qt::ForegroundRole: {
      const auto disabledTextColor =
          qApp->palette().color(QPalette::ColorGroup::Disabled, QPalette::ColorRole::Text);
      switch (index.column()) {
        case COLUMN_AVERAGE:
          if (!anime->score) return disabledTextColor;
          break;
        case COLUMN_DURATION:
          if (anime->episode_length < 1) return disabledTextColor;
          break;
        case COLUMN_SEASON:
          if (!anime->date_started) return disabledTextColor;
          break;
        case COLUMN_TYPE:
          if (anime->type == anime::Type::Unknown) return disabledTextColor;
          break;
        case COLUMN_SCORE:
          if (entry && !entry->score) return disabledTextColor;
          break;
        case COLUMN_STARTED:
          if (entry && !entry->date_started) return disabledTextColor;
          break;
        case COLUMN_COMPLETED:
          if (entry && !entry->date_completed) return disabledTextColor;
          break;
        case COLUMN_LAST_UPDATED:
          if (entry && !entry->last_updated) return disabledTextColor;
          break;
      }
      break;
    }

    case static_cast<int>(AnimeListItemDataRole::Anime): {
      return QVariant::fromValue(anime);
    }
    case static_cast<int>(AnimeListItemDataRole::ListEntry): {
      return QVariant::fromValue(entry);
    }
    case static_cast<int>(AnimeListItemDataRole::Poster): {
      return QVariant::fromValue(imageProvider.loadPoster(anime->id));
    }
  }

  return {};
}

bool AnimeListModel::setData(const QModelIndex& index, const QVariant& value, int role) {
  if (index.isValid() && role == Qt::EditRole) {
    if (index.column() == COLUMN_SCORE) {
      const int id = m_ids.at(index.row());
      if (const auto entry = anime::db.entry(id)) {
        auto updated = *entry;
        updated.score = value.toInt();
        anime::list::save(updated);
      }
      return true;
    }
  }
  return false;
}

QVariant AnimeListModel::headerData(int section, Qt::Orientation orientation, int role) const {
  switch (role) {
    case Qt::DisplayRole: {
      // clang-format off
      switch (section) {
        case COLUMN_TITLE: return tr("Title");
        case COLUMN_PROGRESS: return tr("Progress");
        case COLUMN_DURATION: return tr("Duration");
        case COLUMN_REWATCHES: return tr("Rewatches");
        case COLUMN_SCORE: return tr("Score");
        case COLUMN_AVERAGE: return tr("Average");
        case COLUMN_TYPE: return tr("Type");
        case COLUMN_SEASON: return tr("Season");
        case COLUMN_STARTED: return tr("Started");
        case COLUMN_COMPLETED: return tr("Completed");
        case COLUMN_LAST_UPDATED: return tr("Last updated");
        case COLUMN_NOTES: return tr("Notes");
        case COLUMN_AIRING: return QString{};
      }
      // clang-format on
      break;
    }

    case Qt::ToolTipRole:
      if (section == COLUMN_AIRING) return tr("Airing status");
      break;

    case Qt::TextAlignmentRole: {
      switch (section) {
        case COLUMN_PROGRESS:
        case COLUMN_REWATCHES:
        case COLUMN_SCORE:
        case COLUMN_AVERAGE:
        case COLUMN_TYPE:
          return QVariant(Qt::AlignHCenter | Qt::AlignVCenter);
        case COLUMN_DURATION:
        case COLUMN_SEASON:
        case COLUMN_STARTED:
        case COLUMN_COMPLETED:
        case COLUMN_LAST_UPDATED:
          return QVariant(Qt::AlignRight | Qt::AlignVCenter);
      }
      break;
    }

    case Qt::InitialSortOrderRole: {
      switch (section) {
        case COLUMN_PROGRESS:
        case COLUMN_DURATION:
        case COLUMN_REWATCHES:
        case COLUMN_SCORE:
        case COLUMN_AVERAGE:
        case COLUMN_SEASON:
        case COLUMN_STARTED:
        case COLUMN_COMPLETED:
        case COLUMN_LAST_UPDATED:
          return Qt::DescendingOrder;
        default:
          return Qt::AscendingOrder;
      }
      break;
    }
  }

  return QAbstractListModel::headerData(section, orientation, role);
}

Qt::ItemFlags AnimeListModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) return Qt::NoItemFlags;

  return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
}

bool hasNewEpisode(const Anime& anime, const ListEntry* entry) {
  return entry && !entry->pending_delete && taiga::opt::highlightNewEpisodes.get() &&
         track::availableEpisodes.hasNext(anime.id, entry->watched_episodes);
}

const Anime* AnimeListModel::getAnime(const QModelIndex& index) const {
  if (!index.isValid()) return nullptr;
  return anime::db.item(m_ids.at(index.row()));
}

const ListEntry* AnimeListModel::getListEntry(const QModelIndex& index) const {
  if (!index.isValid()) return nullptr;
  return anime::db.entry(m_ids.at(index.row()));
}

}  // namespace gui
