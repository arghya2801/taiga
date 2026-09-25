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

#include "gui/common/page_widget.hpp"

class QLabel;

namespace gui {

class ScoreChart;

class StatsWidget final : public PageWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(StatsWidget)

public:
  StatsWidget(QWidget* parent);
  ~StatsWidget() = default;

protected:
  void showEvent(QShowEvent* event) override;

private:
  void refresh();

  QLabel* m_listLabel = nullptr;
  QLabel* m_scoreLabel = nullptr;
  QLabel* m_dataLabel = nullptr;
  ScoreChart* m_chart = nullptr;
};

}  // namespace gui
