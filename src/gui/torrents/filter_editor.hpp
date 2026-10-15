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

#include <QWidget>

#include "track/feed_filter.hpp"

class QListWidget;

namespace gui {

// The list of torrent filters, for the settings dialog. Changes apply on `filters()`.
class FilterListEditor final : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(FilterListEditor)

public:
  explicit FilterListEditor(QWidget* parent = nullptr);

  void setFilters(const QList<track::feed::Filter>& filters);
  QList<track::feed::Filter> filters() const;

private:
  void refresh();
  void edit(int row);

  QListWidget* m_list = nullptr;
  QList<track::feed::Filter> m_filters;
};

}  // namespace gui
