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
#include <QJsonObject>
#include <QTimer>

#include "gui/common/page_widget.hpp"
#include "media/account_stats.hpp"
class QLabel;
class QComboBox;
class QSpinBox;
class QTableView;
class QSortFilterProxyModel;
class QTabWidget;
class QLineEdit;
class QPushButton;
class QStandardItemModel;
namespace gui {
class StatsTitlesModel;
class StatsActivityModel;
class StatsChart;
class StatsWidget final : public PageWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(StatsWidget)
public:
  explicit StatsWidget(QWidget* parent);
  void saveState() const;
  bool saveView(const QString& name);

protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private:
  void invalidate();
  void refresh();
  void render();
  void renderGroups();
  void renderTimeline();
  void renderActivity();
  void renderPeriodComparison();
  void updateTitleCount();
  QJsonObject viewState() const;
  void restoreView(const QJsonObject& state);
  void updateSavedViews(const QString& name = {});
  void showTitles(const QList<int>& ids = {}, const QString& label = {}, bool all = true);
  void drillDown(const QList<int>& ids, const QString& label);
  QList<stats::Record> snapshot() const;
  void updateChoices(const QList<stats::Record>& records);
  QComboBox *kind_, *status_, *format_, *year_, *genre_, *studio_, *dimension_, *ranking_,
      *timelineYear_, *periodDimension_, *savedViews_;
  QSpinBox* minimum_;
  QSpinBox *firstFrom_, *firstTo_, *secondFrom_, *secondTo_, *periodMinimum_;
  QLabel *overview_, *scores_, *comparison_, *coverage_, *selection_, *timelineNote_, *tasteNote_,
      *backlog_, *repeats_, *activityNote_, *periodNote_, *viewNotice_, *titlesEmpty_;
  QList<QLabel*> values_, notes_;
  QTabWidget* tabs_;
  QLineEdit* search_;
  QPushButton* showAllGroups_;
  QPushButton *filterToggle_, *updateView_, *deleteView_, *backToChart_;
  QList<StatsChart*> charts_;
  StatsChart* drillSource_ = nullptr;
  int previousTab_ = 0;
  QTableView* titlesView_;
  QTableView* activityView_;
  QTableView* periodView_;
  QStandardItemModel* periodModel_;
  QLineEdit* activitySearch_;
  StatsActivityModel* activityModel_;
  StatsTitlesModel* titlesModel_;
  QSortFilterProxyModel* titlesProxy_;
  stats::Summary summary_;
  stats::ActivitySummary activitySummary_;
  stats::PeriodComparison periodComparison_;
  QString restoredTimelineYear_;
  QList<int> drillIds_;
  QString drillLabel_;
  bool drilled_ = false;
  QTimer debounce_;
  quint64 revision_ = 0;
  bool dirty_ = true, calculating_ = false;
};
}  // namespace gui
