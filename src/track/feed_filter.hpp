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

#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <functional>

// Torrent feed items and the filters that pick which to download. Ported from v1
// `track/feed_filter.cpp`. Kept free of the anime database so it can be checked on its own;
// callers pass what Taiga knows about an item's anime as a `Context`.
namespace track::feed {

enum class ItemState {
  Normal,
  Selected,     // will be downloaded
  Discarded,    // shown greyed out
  Hidden,       // discarded and not shown
};

struct Item {
  QString title;
  QString link;
  QString magnet;
  QString description;
  QString category;
  QString guid;
  qint64 size = 0;
  QDateTime date;

  // Parsed from the title
  int animeId = 0;
  QString animeTitle;
  int episode = 0;  // highest number, 0 if none
  int version = 1;
  QString group;
  QString resolution;
  QString videoType;

  ItemState state = ItemState::Normal;
  bool isDiscarded() const {
    return state == ItemState::Discarded || state == ItemState::Hidden;
  }
};

// What's known about an item's anime. Numbers use the `anime` enum values.
struct Context {
  int watched = 0;
  int episodeCount = 0;
  int listStatus = 0;
  int airingStatus = 0;
  int type = 0;
  QString notes;
  bool episodeAvailable = false;
};

enum class FilterElement {
  AnimeId,
  AnimeStatus,
  AnimeType,
  AnimeEpisodeCount,
  ListStatus,
  ListNotes,
  EpisodeAvailable,
  AnimeTitle,
  EpisodeNumber,
  EpisodeVersion,
  Group,
  Resolution,
  VideoType,
  FileTitle,
  FileCategory,
  FileDescription,
  FileLink,
  FileSize,
};

enum class FilterOperator {
  Equals,
  NotEquals,
  GreaterThan,
  GreaterThanOrEquals,
  LessThan,
  LessThanOrEquals,
  BeginsWith,
  EndsWith,
  Contains,
  NotContains,
};

enum class FilterAction { Discard, Select, Prefer };
enum class FilterMatch { All, Any };
enum class FilterOption { Default, Deactivate, Hide };

struct Condition {
  FilterElement element = FilterElement::FileTitle;
  FilterOperator op = FilterOperator::Equals;
  QString value;  // may use %watched% and %total%
};

struct Filter {
  QString name;
  bool enabled = true;
  FilterAction action = FilterAction::Discard;
  FilterMatch match = FilterMatch::All;
  FilterOption option = FilterOption::Default;
  QList<int> animeIds;  // empty applies to all anime
  QList<Condition> conditions;

  bool matches(const Item& item, const Context& context) const;
};

using ContextFn = std::function<Context(const Item&)>;

// Runs discard/select filters, then preferences, like v1.
void applyFilters(QList<Item>& items, const QList<Filter>& filters, const ContextFn& context);

QList<Filter> defaultFilters();

QString elementName(FilterElement element);
QString operatorName(FilterOperator op);

QJsonArray toJson(const QList<Filter>& filters);
QList<Filter> fromJson(const QJsonArray& array);

}  // namespace track::feed
