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

#include <QObject>
#include <QSet>
#include <QTimer>

#include "track/feed_filter.hpp"

// Torrent RSS feeds: fetch, recognize, filter, download. Ported from v1 `FeedAggregator`.
namespace track::feed {

class Aggregator final : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(Aggregator)

public:
  Aggregator();

  // The source feed from settings. `automatic` checks may download or notify on their own.
  void check(bool automatic = false);
  // The search feed, with %title% replaced.
  void search(const QString& title);

  const QList<Item>& items() const {
    return m_items;
  }
  bool isBusy() const {
    return m_busy;
  }

  void download(const Item& item);
  // Marks the item as handled without downloading it.
  void archive(const Item& item);

  QList<Filter> filters() const;
  void setFilters(const QList<Filter>& filters);
  // Re-runs filters, e.g. after they changed or the list was updated.
  void refilter();
  // Applies the auto-check settings.
  void updateTimer();

signals:
  void itemsChanged();
  void errorOccurred(const QString& message);
  // Emitted after an automatic check with the items that filters selected.
  void newItemsFound(const QList<Item>& items);

private:
  void fetch(const QString& url, bool automatic);
  QList<Item> parse(const QByteArray& data) const;
  void saveArchive() const;

  QList<Item> m_items;
  QList<Item> m_unfiltered;
  QStringList m_archive;  // titles already downloaded or dismissed, newest last
  QTimer m_timer;
  bool m_busy = false;
};

Aggregator* aggregator();

}  // namespace track::feed
