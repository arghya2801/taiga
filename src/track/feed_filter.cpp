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

#include "feed_filter.hpp"

#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <ranges>

#include "base/string.hpp"
#include "media/anime.hpp"
#include "media/anime_list.hpp"

namespace track::feed {

namespace {

// "1080p", "1920x1080" and "1080" all compare as 1080.
int resolutionHeight(const QString& value) {
  static const QRegularExpression re{uR"((?:\d+x)?(\d+))"_s};
  const auto match = re.match(value);
  return match.hasMatch() ? match.captured(1).toInt() : 0;
}

// "700 MB", "1.4 GiB" and plain byte counts.
qint64 parseSize(const QString& value) {
  static const QRegularExpression re{uR"(([\d.]+)\s*([KMGT]?)i?B?)"_s,
                                     QRegularExpression::CaseInsensitiveOption};
  const auto match = re.match(value.trimmed());
  if (!match.hasMatch()) return 0;
  const double number = match.captured(1).toDouble();
  const auto unit = match.captured(2).toUpper();
  const int power = unit.isEmpty() ? 0 : QStringView{u"KMGT"}.indexOf(unit.front()) + 1;
  return static_cast<qint64>(number * std::pow(1024.0, power));
}

bool isNumeric(FilterElement element) {
  switch (element) {
    case FilterElement::AnimeId:
    case FilterElement::AnimeStatus:
    case FilterElement::AnimeType:
    case FilterElement::AnimeEpisodeCount:
    case FilterElement::ListStatus:
    case FilterElement::EpisodeAvailable:
    case FilterElement::EpisodeNumber:
    case FilterElement::EpisodeVersion:
      return true;
    default:
      return false;
  }
}

template <typename T>
bool compare(const T& a, const T& b, FilterOperator op) {
  switch (op) {
    default:
    case FilterOperator::Equals: return a == b;
    case FilterOperator::NotEquals: return a != b;
    case FilterOperator::GreaterThan: return a > b;
    case FilterOperator::GreaterThanOrEquals: return a >= b;
    case FilterOperator::LessThan: return a < b;
    case FilterOperator::LessThanOrEquals: return a <= b;
  }
}

QString elementValue(FilterElement element, const Item& item, const Context& context) {
  switch (element) {
    case FilterElement::AnimeId: return QString::number(item.animeId);
    case FilterElement::AnimeStatus: return QString::number(context.airingStatus);
    case FilterElement::AnimeType: return QString::number(context.type);
    case FilterElement::AnimeEpisodeCount: return QString::number(context.episodeCount);
    case FilterElement::ListStatus: return QString::number(context.listStatus);
    case FilterElement::ListNotes: return context.notes;
    case FilterElement::EpisodeAvailable: return context.episodeAvailable ? u"1"_s : u"0"_s;
    case FilterElement::AnimeTitle: return item.animeTitle;
    // Batches without a number count as the whole series, like v1.
    case FilterElement::EpisodeNumber:
      return QString::number(item.episode ? item.episode : context.episodeCount);
    case FilterElement::EpisodeVersion: return QString::number(item.version);
    case FilterElement::Group: return item.group;
    case FilterElement::Resolution: return item.resolution;
    case FilterElement::VideoType: return item.videoType;
    case FilterElement::FileTitle: return item.title;
    case FilterElement::FileCategory: return item.category;
    case FilterElement::FileDescription: return item.description;
    case FilterElement::FileLink: return item.link;
    case FilterElement::FileSize: return QString::number(item.size);
  }
  return {};
}

bool evaluate(const Condition& condition, const Item& item, const Context& context) {
  const auto element = elementValue(condition.element, item, context);
  auto value = condition.value;
  value.replace(u"%watched%"_s, QString::number(context.watched));
  value.replace(u"%total%"_s, QString::number(context.episodeCount));

  switch (condition.op) {
    case FilterOperator::BeginsWith:
      return element.startsWith(value, Qt::CaseInsensitive);
    case FilterOperator::EndsWith:
      return element.endsWith(value, Qt::CaseInsensitive);
    case FilterOperator::Contains:
      return element.contains(value, Qt::CaseInsensitive);
    case FilterOperator::NotContains:
      return !element.contains(value, Qt::CaseInsensitive);
    default:
      break;
  }

  switch (condition.element) {
    case FilterElement::FileSize:
      return compare(item.size, parseSize(value), condition.op);
    case FilterElement::Resolution:
      if (element.isEmpty()) return condition.op == FilterOperator::NotEquals;
      return compare(resolutionHeight(element), resolutionHeight(value), condition.op);
    default:
      break;
  }

  if (isNumeric(condition.element) && !value.isEmpty()) {
    const int number = value.compare(u"true"_s, Qt::CaseInsensitive) == 0 ? 1 : value.toInt();
    return compare(element.toInt(), number, condition.op);
  }
  if (condition.op == FilterOperator::Equals || condition.op == FilterOperator::NotEquals) {
    return compare(element.compare(value, Qt::CaseInsensitive) == 0, true, condition.op);
  }
  return compare(element.compare(value, Qt::CaseInsensitive), 0, condition.op);
}

void discard(Item& item, FilterOption option) {
  item.state = option == FilterOption::Hide ? ItemState::Hidden : ItemState::Discarded;
}

bool sameRelease(const Item& a, const Item& b) {
  const bool sameAnime = a.animeId || b.animeId
                             ? a.animeId == b.animeId
                             : a.animeTitle.compare(b.animeTitle, Qt::CaseInsensitive) == 0;
  return sameAnime && a.episode == b.episode;
}

}  // namespace

bool Filter::matches(const Item& item, const Context& context) const {
  if (!animeIds.isEmpty() && !animeIds.contains(item.animeId)) return false;
  const auto test = [&](const Condition& c) { return evaluate(c, item, context); };
  return match == FilterMatch::All ? std::ranges::all_of(conditions, test)
                                   : std::ranges::any_of(conditions, test);
}

void applyFilters(QList<Item>& items, const QList<Filter>& filters, const ContextFn& context) {
  QList<Context> contexts;
  contexts.reserve(items.size());
  for (const auto& item : items) contexts.append(context(item));

  for (const auto& filter : filters) {
    if (!filter.enabled || filter.action == FilterAction::Prefer) continue;
    for (qsizetype i = 0; i < items.size(); ++i) {
      auto& item = items[i];
      if (item.isDiscarded() || !filter.matches(item, contexts[i])) continue;
      if (filter.action == FilterAction::Discard) {
        discard(item, filter.option);
      } else {
        item.state = ItemState::Selected;
      }
    }
  }

  // Preferences: when some releases of an episode match, the others are dropped. A filter
  // limited to specific anime also drops non-matching releases that have no alternative.
  for (const auto& filter : filters) {
    if (!filter.enabled || filter.action != FilterAction::Prefer) continue;
    for (qsizetype i = 0; i < items.size(); ++i) {
      if (items[i].isDiscarded()) continue;
      if (!filter.animeIds.isEmpty() && !filter.animeIds.contains(items[i].animeId)) continue;
      if (filter.matches(items[i], contexts[i])) continue;

      const bool strong = !filter.animeIds.isEmpty();
      const bool betterExists = std::ranges::any_of(
          std::views::iota(qsizetype{0}, items.size()), [&](qsizetype j) {
            return j != i && !items[j].isDiscarded() && sameRelease(items[i], items[j]) &&
                   filter.matches(items[j], contexts[j]);
          });
      if (strong || betterExists) discard(items[i], filter.option);
    }
  }
}

QList<Filter> defaultFilters() {
  using anime::list::Status;
  const auto status = [](Status s) { return QString::number(static_cast<int>(s)); };
  return {
      {.name = u"Discard and deactivate not-in-list anime"_s,
       .action = FilterAction::Discard,
       .match = FilterMatch::Any,
       .option = FilterOption::Deactivate,
       .conditions = {{FilterElement::ListStatus, FilterOperator::Equals,
                       status(Status::NotInList)}}},
      {.name = u"Discard watched and available episodes"_s,
       .action = FilterAction::Discard,
       .match = FilterMatch::Any,
       .conditions = {{FilterElement::EpisodeNumber, FilterOperator::LessThanOrEquals,
                       u"%watched%"_s},
                      {FilterElement::EpisodeAvailable, FilterOperator::Equals, u"True"_s}}},
      {.name = u"Discard dropped"_s,
       .action = FilterAction::Discard,
       .conditions = {{FilterElement::ListStatus, FilterOperator::Equals,
                       status(Status::Dropped)}}},
      {.name = u"Select currently watching"_s,
       .action = FilterAction::Select,
       .match = FilterMatch::Any,
       .conditions = {{FilterElement::ListStatus, FilterOperator::Equals,
                       status(Status::Watching)}}},
      {.name = u"Select airing anime in plan to watch"_s,
       .action = FilterAction::Select,
       .conditions = {{FilterElement::AnimeStatus, FilterOperator::Equals,
                       QString::number(static_cast<int>(anime::Status::Airing))},
                      {FilterElement::ListStatus, FilterOperator::Equals,
                       status(Status::PlanToWatch)}}},
      {.name = u"Prefer high-resolution files"_s,
       .action = FilterAction::Prefer,
       .match = FilterMatch::Any,
       .conditions = {{FilterElement::Resolution, FilterOperator::Equals, u"1080p"_s}}},
  };
}

QString elementName(FilterElement element) {
  switch (element) {
    case FilterElement::AnimeId: return u"Anime ID"_s;
    case FilterElement::AnimeStatus: return u"Airing status"_s;
    case FilterElement::AnimeType: return u"Anime type"_s;
    case FilterElement::AnimeEpisodeCount: return u"Episode count"_s;
    case FilterElement::ListStatus: return u"List status"_s;
    case FilterElement::ListNotes: return u"List notes"_s;
    case FilterElement::EpisodeAvailable: return u"Episode available"_s;
    case FilterElement::AnimeTitle: return u"Anime title"_s;
    case FilterElement::EpisodeNumber: return u"Episode number"_s;
    case FilterElement::EpisodeVersion: return u"Episode version"_s;
    case FilterElement::Group: return u"Release group"_s;
    case FilterElement::Resolution: return u"Video resolution"_s;
    case FilterElement::VideoType: return u"Video type"_s;
    case FilterElement::FileTitle: return u"File title"_s;
    case FilterElement::FileCategory: return u"File category"_s;
    case FilterElement::FileDescription: return u"File description"_s;
    case FilterElement::FileLink: return u"File link"_s;
    case FilterElement::FileSize: return u"File size"_s;
  }
  return {};
}

QString operatorName(FilterOperator op) {
  switch (op) {
    case FilterOperator::Equals: return u"is"_s;
    case FilterOperator::NotEquals: return u"is not"_s;
    case FilterOperator::GreaterThan: return u"is greater than"_s;
    case FilterOperator::GreaterThanOrEquals: return u"is greater than or equal to"_s;
    case FilterOperator::LessThan: return u"is less than"_s;
    case FilterOperator::LessThanOrEquals: return u"is less than or equal to"_s;
    case FilterOperator::BeginsWith: return u"begins with"_s;
    case FilterOperator::EndsWith: return u"ends with"_s;
    case FilterOperator::Contains: return u"contains"_s;
    case FilterOperator::NotContains: return u"doesn't contain"_s;
  }
  return {};
}

QJsonArray toJson(const QList<Filter>& filters) {
  QJsonArray array;
  for (const auto& filter : filters) {
    QJsonArray conditions;
    for (const auto& c : filter.conditions) {
      conditions.append(QJsonObject{{"element", static_cast<int>(c.element)},
                                    {"operator", static_cast<int>(c.op)},
                                    {"value", c.value}});
    }
    QJsonArray ids;
    for (const int id : filter.animeIds) ids.append(id);
    array.append(QJsonObject{{"name", filter.name},
                             {"enabled", filter.enabled},
                             {"action", static_cast<int>(filter.action)},
                             {"match", static_cast<int>(filter.match)},
                             {"option", static_cast<int>(filter.option)},
                             {"animeIds", ids},
                             {"conditions", conditions}});
  }
  return array;
}

QList<Filter> fromJson(const QJsonArray& array) {
  QList<Filter> filters;
  for (const auto& value : array) {
    const auto object = value.toObject();
    Filter filter{
        .name = object["name"].toString(),
        .enabled = object["enabled"].toBool(true),
        .action = static_cast<FilterAction>(object["action"].toInt()),
        .match = static_cast<FilterMatch>(object["match"].toInt()),
        .option = static_cast<FilterOption>(object["option"].toInt()),
    };
    for (const auto& id : object["animeIds"].toArray()) filter.animeIds.append(id.toInt());
    for (const auto& c : object["conditions"].toArray()) {
      const auto o = c.toObject();
      filter.conditions.append({static_cast<FilterElement>(o["element"].toInt()),
                                static_cast<FilterOperator>(o["operator"].toInt()),
                                o["value"].toString()});
    }
    filters.append(filter);
  }
  return filters;
}

}  // namespace track::feed
