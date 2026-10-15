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

#include "stats_widget.hpp"

#include <QAbstractTableModel>
#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QtConcurrentRun>
#include <algorithm>
#include <functional>

#include "gui/main/main_window.hpp"
#include "gui/media/media_dialog.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/theme.hpp"
#include "media/account_activity.hpp"
#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"
#include "media/manga_library.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"

namespace gui {
namespace {
enum Chart {
  Status,
  OverviewScore,
  Genres,
  Formats,
  Backlog,
  Repeats,
  RatingsScore,
  Difference,
  Above,
  Below,
  Taste,
  Years,
  Months,
  Releases,
  Length,
  Progress,
  DailyActivity,
  MonthlyActivity
};
constexpr int kTitlesTab = 5;
enum Role { ValueRole = Qt::UserRole + 1, RatioRole, NoteRole, IdsRole, ValueTextRole };
struct StatsResult {
  stats::Summary summary;
  stats::ActivitySummary activity;
  stats::PeriodComparison periods;
};
QString hours(qint64 seconds) {
  return QLocale().toString(seconds / 3600.0, 'f', 1);
}
QString number(double value) {
  return QLocale().toString(value, 'f', 2);
}
QString count(qint64 value) {
  return QLocale().toString(value);
}
QColor blue() {
  return theme.isDark() ? QColor(0x60a5fa) : QColor(0x1f6fd1);
}
FuzzyDate fuzzyDate(QString text) {
  if (text.size() == 4)
    text += "-00-00";
  else if (text.size() == 7)
    text += "-00";
  return FuzzyDate(text.toStdString());
}
QString yearKey(const FuzzyDate& date) {
  return date.year() ? QString::number(date.year()) : QString{};
}
QString monthKey(const FuzzyDate& date) {
  return date.year() && date.month() >= 1 && date.month() <= 12
             ? QStringLiteral("%1-%2")
                   .arg(date.year(), 4, 10, QChar('0'))
                   .arg(date.month(), 2, 10, QChar('0'))
             : QString{};
}
QDate exactDate(const FuzzyDate& date) {
  return QDate(date.year(), date.month(), date.day());
}
QStringList strings(const std::vector<std::string>& list) {
  QStringList result;
  for (const auto& text : list) result.append(QString::fromStdString(text));
  return result;
}
QString mangaStatus(const QString& status) {
  for (const auto& [key, label] : manga::kStatuses)
    if (status == QLatin1String(key)) return QLatin1String(label);
  return QStringLiteral("Unknown");
}

// Reflow the same cards instead of rebuilding charts when the window changes size.
class StatsGrid : public QWidget {
public:
  explicit StatsGrid(int minimum, int maximum, QWidget* parent = nullptr)
      : QWidget(parent), minimum_(minimum), maximum_(maximum), grid_(new QGridLayout(this)) {
    grid_->setContentsMargins(0, 0, 0, 0);
    grid_->setSpacing(16);
  }
  void add(QWidget* widget) {
    widgets_.append(widget);
    columns_ = 0;
    arrange();
  }

protected:
  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    arrange();
  }

private:
  void arrange() {
    const int columns = std::clamp((width() + 16) / (minimum_ + 16), 1, maximum_);
    if (columns_ == columns) return;
    columns_ = columns;
    for (int i = 0; i < widgets_.size(); ++i) {
      grid_->removeWidget(widgets_[i]);
      grid_->addWidget(widgets_[i], i / columns, i % columns);
    }
    for (int i = 0; i < maximum_; ++i) grid_->setColumnStretch(i, i < columns ? 1 : 0);
  }
  int minimum_, maximum_;
  int columns_ = 0;
  QGridLayout* grid_;
  QList<QWidget*> widgets_;
};

class StatsCard : public QFrame {
public:
  StatsCard(const QString& title, const QString& description, QWidget* parent = nullptr)
      : QFrame(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(18, 16, 18, 16);
    box->setSpacing(10);
    auto* heading = new QLabel(title, this);
    QFont font = heading->font();
    font.setPointSize(12);
    font.setWeight(QFont::DemiBold);
    heading->setFont(font);
    heading->setWordWrap(true);
    box->addWidget(heading);
    if (!description.isEmpty()) {
      auto* note = new QLabel(description, this);
      note->setWordWrap(true);
      box->addWidget(note);
    }
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(theme.color(Theme::Color::Line));
    p.setBrush(theme.color(Theme::Color::Surface));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
  }
};

class ChartDelegate : public QStyledItemDelegate {
public:
  ChartDelegate(bool columns, QObject* parent) : QStyledItemDelegate(parent), columns_(columns) {}
  QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
    return columns_ ? QSize(48, 180) : QSize(220, 54);
  }
  void paint(QPainter* p, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const auto area = option.rect.adjusted(5, 3, -5, -3);
    if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver))
      p->fillRect(option.rect, theme.color(Theme::Color::Raised));
    p->setFont(option.font);
    const QFontMetrics fm(option.font);
    const auto label = index.data(Qt::UserRole).toString();
    const auto value = index.data(ValueTextRole).toString();
    const auto note = index.data(NoteRole).toString();
    const double ratio = index.data(RatioRole).toDouble();
    p->setPen(theme.color(Theme::Color::Text));
    if (columns_) {
      const QRect baseline(area.left(), area.bottom() - 24, area.width(), 1);
      p->fillRect(baseline, theme.color(Theme::Color::Line));
      const int available = std::max(0, area.height() - 56);
      const int height = int(available * ratio);
      const QRect bar(area.left() + std::max(2, area.width() / 6), baseline.top() - height,
                      std::max(4, area.width() * 2 / 3), height);
      p->fillRect(bar, blue());
      p->drawText(QRect(area.left() - 3, bar.top() - 25, area.width() + 6, 22), Qt::AlignCenter,
                  value);
      p->drawText(QRect(area.left() - 3, baseline.top() + 2, area.width() + 6, 22), Qt::AlignCenter,
                  fm.elidedText(label, Qt::ElideRight, area.width() + 6));
    } else {
      const int valueWidth = fm.horizontalAdvance(value) + 12;
      p->drawText(QRect(area.left(), area.top(), std::max(0, area.width() - valueWidth), 22),
                  Qt::AlignLeft | Qt::AlignVCenter,
                  fm.elidedText(label, Qt::ElideRight, std::max(0, area.width() - valueWidth)));
      p->drawText(QRect(area.right() - valueWidth, area.top(), valueWidth, 22),
                  Qt::AlignRight | Qt::AlignVCenter, value);
      p->setPen(theme.color(Theme::Color::Muted));
      p->drawText(QRect(area.left(), area.top() + 22, area.width(), 19),
                  Qt::AlignLeft | Qt::AlignVCenter,
                  fm.elidedText(note, Qt::ElideRight, area.width()));
      const QRect track(area.left(), area.bottom() - 5, area.width(), 4);
      p->fillRect(track, theme.color(Theme::Color::Sunken));
      p->fillRect(QRect(track.left(), track.top(), int(track.width() * ratio), 4), blue());
    }
    if (option.state & QStyle::State_HasFocus) {
      p->setPen(QPen(theme.color(Theme::Color::Accent), 2));
      p->setBrush(Qt::NoBrush);
      p->drawRoundedRect(option.rect.adjusted(1, 1, -2, -2), 3, 3);
    }
    p->restore();
  }

private:
  bool columns_;
};

struct ChartRow {
  QString label, value, note;
  double amount = 0;
  QList<int> ids;
};
}  // namespace

// Native list items provide keyboard navigation and screen-reader values for each bar.
class StatsChart final : public QListView {
public:
  StatsChart(bool columns, int visibleRows, QWidget* parent)
      : QListView(parent),
        columns_(columns),
        visibleRows_(visibleRows),
        model_(new QStandardItemModel(this)) {
    setModel(model_);
    setItemDelegate(new ChartDelegate(columns, this));
    setFrameShape(QFrame::NoFrame);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setMouseTracking(true);
    setUniformItemSizes(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet("QListView { background: transparent; border: none; }");
    setAccessibleDescription(tr("Use arrow keys to select a bar and Enter to view its titles."));
    if (columns_) {
      setViewMode(QListView::IconMode);
      setFlow(QListView::LeftToRight);
      setWrapping(false);
      setMovement(QListView::Static);
      setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    } else
      setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const auto open = [this](const QModelIndex& index) {
      if (index.isValid() && activate)
        activate(index.data(IdsRole).value<QList<int>>(), index.data(Qt::UserRole).toString(),
                 index.data(NoteRole).toString());
    };
    connect(this, &QListView::clicked, this, open);
    connect(this, &QListView::activated, this, open);
  }
  std::function<void(const QList<int>&, const QString&, const QString&)> activate;
  void setEmptyText(const QString& text) {
    emptyText_ = text;
  }
  void setMinimumColumnWidth(int width) {
    minimumColumnWidth_ = width;
    fitColumns();
  }
  void setRows(const QList<ChartRow>& rows) {
    model_->clear();
    double maximum = 0;
    for (const auto& row : rows) maximum = std::max(maximum, std::abs(row.amount));
    for (const auto& row : rows) {
      auto* item = new QStandardItem(row.label + ": " + row.value + ". " + row.note);
      item->setData(row.label, Qt::UserRole);
      item->setData(row.amount, ValueRole);
      item->setData(maximum > 0 ? std::abs(row.amount) / maximum : 0.0, RatioRole);
      item->setData(row.value, ValueTextRole);
      item->setData(row.note, NoteRole);
      item->setData(QVariant::fromValue(row.ids), IdsRole);
      item->setData(item->text(), Qt::AccessibleTextRole);
      item->setToolTip(item->text() + tr("\nClick or press Enter to view titles."));
      item->setEditable(false);
      model_->appendRow(item);
    }
    setFixedHeight(columns_ ? 204
                            : std::max(108, std::min(visibleRows_, int(rows.size())) * 54 + 4));
    setAccessibleDescription(
        rows.isEmpty() ? emptyText_
                       : tr("Use arrow keys to select a bar and Enter to view its titles."));
    fitColumns();
    viewport()->update();
  }

protected:
  void resizeEvent(QResizeEvent* event) override {
    QListView::resizeEvent(event);
    fitColumns();
  }
  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Space && currentIndex().isValid()) {
      emit activated(currentIndex());
      event->accept();
    } else
      QListView::keyPressEvent(event);
  }
  void paintEvent(QPaintEvent* event) override {
    QListView::paintEvent(event);
    if (!model_->rowCount()) {
      QPainter p(viewport());
      p.setPen(theme.color(Theme::Color::Muted));
      p.drawText(viewport()->rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                 emptyText_);
    }
  }

private:
  void fitColumns() {
    if (columns_)
      setGridSize(QSize(
          std::max(minimumColumnWidth_, viewport()->width() / std::max(1, model_->rowCount())),
          180));
  }
  bool columns_;
  int visibleRows_;
  int minimumColumnWidth_ = 38;
  QString emptyText_ =
      tr("No titles match this chart. Try clearing the filters or synchronizing your list.");
  QStandardItemModel* model_;
};

// Rows are values in a snapshot, rather than thousands of QStandardItem objects.
class StatsTitlesModel final : public QAbstractTableModel {
public:
  using QAbstractTableModel::QAbstractTableModel;
  QList<stats::Record> rows;
  void setRows(QList<stats::Record> value) {
    beginResetModel();
    rows = std::move(value);
    endResetModel();
  }
  int rowCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() ? 0 : int(rows.size());
  }
  int columnCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() ? 0 : 10;
  }
  QVariant headerData(int section, Qt::Orientation o, int role) const override {
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    const QStringList labels{tr("Title"),     tr("Status"),      tr("Format"),   tr("Your score"),
                             tr("Community"), tr("Difference"),  tr("Progress"), tr("Release year"),
                             tr("Completed"), tr("Last updated")};
    return labels.value(section);
  }
  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.row() >= rows.size()) return {};
    const auto& r = rows[index.row()];
    if (role == Qt::UserRole + 1) return r.id;
    if (role == Qt::TextAlignmentRole)
      return int(index.column() ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
    if (role != Qt::DisplayRole && role != Qt::UserRole) return {};
    const bool raw = role == Qt::UserRole;
    switch (index.column()) {
      case 0:
        return r.title;
      case 1:
        return r.status;
      case 2:
        return r.format;
      case 3:
        return raw ? QVariant(r.score) : QVariant(r.score > 0 ? number(r.score) : tr("Unrated"));
      case 4:
        return raw ? QVariant(r.communityScore)
                   : QVariant(r.communityScore > 0 ? number(r.communityScore) : tr("Unknown"));
      case 5:
        return raw ? QVariant(r.score > 0 && r.communityScore > 0 ? r.score - r.communityScore
                                                                  : -100.0)
                   : QVariant(r.score > 0 && r.communityScore > 0
                                  ? number(r.score - r.communityScore)
                                  : QStringLiteral("-"));
      case 6:
        return raw ? QVariant(r.progress)
                   : QVariant(
                         QStringLiteral("%1 / %2")
                             .arg(r.progress)
                             .arg(r.total > 0 ? QString::number(r.total) : QStringLiteral("?")));
      case 7:
        return r.releaseYear;
      case 8:
        return r.completionMonth.isEmpty() ? r.completionYear : r.completionMonth;
      case 9:
        return r.updated.toString(Qt::ISODate);
    }
    return {};
  }
};

class StatsActivityModel final : public QAbstractTableModel {
public:
  using QAbstractTableModel::QAbstractTableModel;
  QList<stats::ActivityChange> rows;
  QHash<int, QString> titles;
  void setRows(const stats::ActivitySummary& activity, const QList<stats::Record>& records) {
    beginResetModel();
    rows = activity.changes;
    titles.clear();
    for (const auto& record : records) titles.insert(record.id, record.title);
    endResetModel();
  }
  int rowCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() ? 0 : int(rows.size());
  }
  int columnCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() ? 0 : 4;
  }
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    return QStringList{tr("Recorded at"), tr("Title"), tr("Progress change"), tr("Score change")}
        .value(section);
  }
  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.row() >= rows.size()) return {};
    const auto& row = rows[index.row()];
    if (role == Qt::UserRole + 1) return row.mediaId;
    if (role != Qt::DisplayRole && role != Qt::UserRole) return {};
    if (role == Qt::UserRole) {
      switch (index.column()) {
        case 0:
          return row.time.toMSecsSinceEpoch();
        case 1:
          return titles.value(row.mediaId);
        case 2:
          return row.progress - row.previousProgress;
        case 3:
          return row.score - row.previousScore;
      }
    }
    const auto score = [](double value) {
      return value > 0 ? number(value) : StatsWidget::tr("Unrated");
    };
    switch (index.column()) {
      case 0:
        return QLocale().toString(row.time.toLocalTime(), QLocale::ShortFormat);
      case 1:
        return titles.value(row.mediaId, tr("Unknown title #%1").arg(row.mediaId));
      case 2:
        return row.previousProgress == row.progress
                   ? QStringLiteral("—")
                   : tr("%1 → %2").arg(count(row.previousProgress), count(row.progress));
      case 3:
        return row.previousScore == row.score ? QStringLiteral("—")
                                              : score(row.previousScore) + " → " + score(row.score);
    }
    return {};
  }
};

StatsWidget::StatsWidget(QWidget* parent) : PageWidget(parent) {
  auto font = this->font();
  font.setPointSize(10);
  setFont(font);
  auto* heading = new QWidget(this);
  auto* headingBox = new QVBoxLayout(heading);
  headingBox->setContentsMargins(0, 0, 0, 0);
  auto* title = new QLabel(tr("Your statistics"), heading);
  font.setPointSize(22);
  font.setWeight(QFont::DemiBold);
  title->setFont(font);
  headingBox->addWidget(title);
  overview_ = new QLabel(tr("Loading your saved list…"), heading);
  overview_->setWordWrap(true);
  headingBox->addWidget(overview_);
  m_toolbarLayout->insertWidget(0, heading, 1);
  kind_ = new QComboBox(this);
  kind_->setObjectName("statsKind");
  kind_->setAccessibleName(tr("Statistics list"));
  kind_->addItems({tr("Anime"), tr("Manga")});
  m_toolbarLayout->insertWidget(1, kind_);
  auto* filterToggle = filterToggle_ = new QPushButton(tr("Filters"), this);
  filterToggle->setCheckable(true);
  filterToggle->setObjectName("statsFilterToggle");
  m_toolbarLayout->insertWidget(2, filterToggle);
  m_toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
  m_toolbar->addAction(theme.getIcon("sync"), tr("Sync list"), this, [this] {
    if (kind_->currentIndex())
      manga::library.refresh();
    else if (auto* window = mainWindow())
      QMetaObject::invokeMethod(window, "synchronize");
  });
  auto* savedBar = new QHBoxLayout;
  savedBar->setContentsMargins(20, 0, 20, 8);
  savedViews_ = new QComboBox(this);
  savedViews_->setObjectName("statsSavedViews");
  savedViews_->setAccessibleName(tr("Saved statistics views"));
  savedViews_->setMinimumContentsLength(14);
  savedViews_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  savedBar->addWidget(savedViews_, 1);
  auto* saveViewButton = new QPushButton(tr("Save view..."), this);
  saveViewButton->setObjectName("statsSaveView");
  updateView_ = new QPushButton(tr("Update"), this);
  updateView_->setObjectName("statsUpdateView");
  updateView_->setToolTip(tr("Replace the selected saved view with the current settings."));
  deleteView_ = new QPushButton(tr("Delete"), this);
  deleteView_->setObjectName("statsDeleteView");
  for (auto* button : {saveViewButton, updateView_, deleteView_}) savedBar->addWidget(button);
  static_cast<QVBoxLayout*>(layout())->addLayout(savedBar);
  viewNotice_ = new QLabel(this);
  viewNotice_->setObjectName("statsViewNotice");
  viewNotice_->setTextFormat(Qt::PlainText);
  viewNotice_->setWordWrap(true);
  viewNotice_->setContentsMargins(20, 0, 20, 4);
  viewNotice_->hide();
  layout()->addWidget(viewNotice_);
  auto* controls = new QWidget(this);
  controls->setObjectName("statsFilters");
  auto* grid = new QGridLayout(controls);
  grid->setContentsMargins(20, 8, 20, 16);
  const auto combo = [&](const QString& label, const QString& name, int column, int row) {
    auto* box = new QComboBox(controls);
    box->setObjectName(name);
    box->setAccessibleName(label);
    box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    box->setMinimumContentsLength(10);
    auto* caption = new QLabel(label, controls);
    caption->setBuddy(box);
    grid->addWidget(caption, row * 2, column);
    grid->addWidget(box, row * 2 + 1, column);
    return box;
  };
  status_ = combo(tr("Status"), "statsStatus", 0, 0);
  format_ = combo(tr("Format"), "statsFormat", 1, 0);
  year_ = combo(tr("Release year"), "statsReleaseYear", 2, 0);
  genre_ = combo(tr("Genre"), "statsGenre", 0, 1);
  studio_ = combo(tr("Studio / author"), "statsStudio", 1, 1);
  auto* reset = new QPushButton(tr("Clear filters"), controls);
  grid->addWidget(reset, 3, 2);
  layout()->addWidget(controls);
  controls->hide();
  connect(filterToggle, &QPushButton::toggled, controls, &QWidget::setVisible);
  connect(reset, &QPushButton::clicked, this, [this] {
    for (auto* box : {status_, format_, year_, genre_, studio_}) {
      QSignalBlocker block(box);
      box->setCurrentIndex(0);
    }
    invalidate();
  });
  for (auto* box : {status_, format_, year_, genre_, studio_})
    connect(box, &QComboBox::currentIndexChanged, this, [this] { invalidate(); });
  connect(kind_, &QComboBox::currentIndexChanged, this, [this] {
    for (auto* box : {status_, format_, year_, genre_, studio_}) {
      const QSignalBlocker block(box);
      box->setCurrentIndex(0);
    }
    search_->clear();
    activitySearch_->clear();
    const QSignalBlocker timelineBlock(timelineYear_);
    timelineYear_->clear();
    restoredTimelineYear_.clear();
    drilled_ = false;
    drillIds_.clear();
    drillLabel_.clear();
    invalidate();
  });

  auto* metrics = new StatsGrid(165, 4, this);
  metrics->setContentsMargins(20, 12, 20, 20);
  for (int i = 0; i < 4; ++i) {
    auto* metric = new QWidget(metrics);
    auto* box = new QVBoxLayout(metric);
    box->setContentsMargins(0, 0, 0, 0);
    auto* value = new QLabel("—", metric);
    auto valueFont = value->font();
    valueFont.setPointSize(24);
    valueFont.setWeight(QFont::DemiBold);
    value->setFont(valueFont);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* note = new QLabel(metric);
    note->setWordWrap(true);
    box->addWidget(value);
    box->addWidget(note);
    values_.append(value);
    notes_.append(note);
    metrics->add(metric);
  }
  layout()->addWidget(metrics);
  tabs_ = new QTabWidget(this);
  tabs_->setObjectName("statsTabs");
  tabs_->setDocumentMode(true);
  layout()->addWidget(tabs_);
  const auto page = [&](const QString& name) {
    auto* area = new QScrollArea(tabs_);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(area);
    auto* box = new QVBoxLayout(content);
    box->setContentsMargins(20, 20, 20, 24);
    box->setSpacing(16);
    area->setWidget(content);
    tabs_->addTab(area, name);
    return box;
  };
  charts_.resize(18);
  const auto chart = [&](StatsCard* card, int index, bool columns = false, int rows = 6) {
    auto* view = new StatsChart(columns, rows, card);
    view->setObjectName(QStringLiteral("statsChart%1").arg(index));
    view->setAccessibleName(card->findChild<QLabel*>()->text());
    view->activate = [this, index](const QList<int>& ids, const QString& label,
                                   const QString& note) {
      drillSource_ = charts_[index];
      drillDown(ids, charts_[index]->accessibleName() + ": " +
                         (index == DailyActivity || index == MonthlyActivity ? note : label));
    };
    card->layout()->addWidget(view);
    static_cast<QVBoxLayout*>(card->layout())->addStretch();
    charts_[index] = view;
    return card;
  };
  auto* overviewBox = page(tr("Overview"));
  auto* overviewGrid = new StatsGrid(400, 2);
  overviewBox->addWidget(overviewGrid);
  overviewGrid->add(chart(
      new StatsCard(tr("Where your list stands"), tr("Select a status to explore its titles.")),
      Status));
  overviewGrid->add(
      chart(new StatsCard(tr("How you rate"),
                          tr("Your scores from 1 to 10. Unrated titles are excluded.")),
            OverviewScore, true));
  overviewGrid->add(
      chart(new StatsCard(tr("Genres in your list"),
                          tr("Most titles in your selection. A title can have several genres.")),
            Genres));
  overviewGrid->add(chart(
      new StatsCard(tr("Your preferred formats"), tr("TV, movies and everything in between.")),
      Formats));
  auto* backlog = new StatsCard(tr("Your biggest unfinished titles"), {});
  backlog_ = new QLabel(backlog);
  backlog_->setWordWrap(true);
  backlog->layout()->addWidget(backlog_);
  auto* stalled = new QPushButton(tr("Explore stalled titles"), backlog);
  connect(stalled, &QPushButton::clicked, this, [this] {
    drillSource_ = nullptr;
    QList<int> ids;
    for (const auto& r : summary_.records)
      if ((r.repeating || (r.status != "Completed" && r.status != "Dropped")) &&
          r.updated.isValid() && r.updated.daysTo(QDate::currentDate()) >= 180)
        ids.append(r.id);
    drillDown(ids, tr("Unfinished titles unchanged for 180+ days"));
  });
  backlog->layout()->addWidget(stalled);
  overviewGrid->add(chart(backlog, Backlog));
  auto* repeats = new StatsCard(tr("Titles worth revisiting"), {});
  repeats_ = new QLabel(repeats);
  repeats_->setWordWrap(true);
  repeats->layout()->addWidget(repeats_);
  overviewGrid->add(chart(repeats, Repeats));
  auto* coverageToggle = new QPushButton(tr("About these numbers"));
  coverageToggle->setCheckable(true);
  overviewBox->addWidget(coverageToggle, 0, Qt::AlignLeft);
  coverage_ = new QLabel;
  coverage_->setWordWrap(true);
  coverage_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  overviewBox->addWidget(coverage_);
  coverage_->hide();
  connect(coverageToggle, &QPushButton::toggled, coverage_, &QWidget::setVisible);
  overviewBox->addStretch();

  auto* ratingsBox = page(tr("Ratings"));
  auto* ratingSummary = new StatsCard(tr("Your rating style"), {});
  scores_ = new QLabel(ratingSummary);
  scores_->setWordWrap(true);
  scores_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  ratingSummary->layout()->addWidget(scores_);
  ratingsBox->addWidget(ratingSummary);
  auto* ratingsGrid = new StatsGrid(400, 2);
  ratingsBox->addWidget(ratingsGrid);
  ratingsGrid->add(
      chart(new StatsCard(tr("Score distribution"), tr("Exact counts behind every rating.")),
            RatingsScore, true));
  auto* comparison = new StatsCard(
      tr("You compared with MyAnimeList"),
      tr("Your score minus MAL’s mean for the same title. Buckets round to the nearest point."));
  comparison_ = new QLabel(comparison);
  comparison_->setWordWrap(true);
  comparison->layout()->addWidget(comparison_);
  ratingsGrid->add(chart(comparison, Difference, true));
  ratingsGrid->add(chart(
      new StatsCard(tr("You liked these more"),
                    tr("Largest positive differences from MAL. Select a title to inspect it.")),
      Above));
  ratingsGrid->add(
      chart(new StatsCard(
                tr("You liked these less"),
                tr("Largest negative differences from MAL. Only titles you rated are compared.")),
            Below));
  ratingsBox->addStretch();

  auto* tasteBox = page(tr("Taste"));
  auto* periods = new StatsCard(
      tr("How your taste changes"),
      tr("Compare completed titles from two periods using your finish years and current ratings. "
         "This compares what you completed, not how you rated it at the time."));
  auto* periodControls = new QGridLayout;
  const auto yearBox = [&](const QString& name, const QString& label, int year) {
    auto* box = new QSpinBox(periods);
    box->setObjectName(name);
    box->setAccessibleName(label);
    box->setRange(1, 9999);
    box->setValue(year);
    return box;
  };
  const int currentYear = QDate::currentDate().year();
  firstFrom_ = yearBox("statsFirstFrom", tr("First period, from year"), currentYear - 5);
  firstTo_ = yearBox("statsFirstTo", tr("First period, through year"), currentYear - 3);
  secondFrom_ = yearBox("statsSecondFrom", tr("Second period, from year"), currentYear - 2);
  secondTo_ = yearBox("statsSecondTo", tr("Second period, through year"), currentYear);
  for (int row = 0; row < 2; ++row) {
    auto* caption = new QLabel(row ? tr("Period B") : tr("Period A"), periods);
    auto* from = row ? secondFrom_ : firstFrom_;
    auto* through = row ? secondTo_ : firstTo_;
    caption->setBuddy(from);
    periodControls->addWidget(caption, row, 0);
    periodControls->addWidget(from, row, 1);
    periodControls->addWidget(new QLabel(tr("through"), periods), row, 2);
    periodControls->addWidget(through, row, 3);
  }
  periodDimension_ = new QComboBox(periods);
  periodDimension_->setObjectName("statsPeriodDimension");
  periodDimension_->setAccessibleName(tr("Compare taste category"));
  for (const auto& [label, dimension] :
       QList<QPair<QString, stats::Dimension>>{{tr("Genres"), stats::Dimension::Genre},
                                               {tr("Studios / authors"), stats::Dimension::Studio},
                                               {tr("Formats"), stats::Dimension::Format}})
    periodDimension_->addItem(label, int(dimension));
  periodMinimum_ = new QSpinBox(periods);
  periodMinimum_->setObjectName("statsPeriodMinimum");
  periodMinimum_->setRange(1, 100000);
  periodMinimum_->setValue(3);
  periodMinimum_->setPrefix(tr("Min. ratings per period: "));
  periodMinimum_->setAccessibleName(tr("Minimum ratings in each period for a score change"));
  periodMinimum_->setToolTip(
      tr("Counts and shares always appear. Score changes need this many "
         "rated titles in both periods."));
  periodControls->addWidget(periodDimension_, 2, 0, 1, 2);
  periodControls->addWidget(periodMinimum_, 2, 2, 1, 2);
  static_cast<QVBoxLayout*>(periods->layout())->addLayout(periodControls);
  periodNote_ = new QLabel(periods);
  periodNote_->setObjectName("statsPeriodNote");
  periodNote_->setWordWrap(true);
  periodNote_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  periods->layout()->addWidget(periodNote_);
  periodModel_ = new QStandardItemModel(this);
  periodModel_->setHorizontalHeaderLabels({tr("Category"), tr("A titles / share"), tr("A mean"),
                                           tr("B titles / share"), tr("B mean"), tr("Share B - A"),
                                           tr("Score B - A")});
  auto* periodProxy = new QSortFilterProxyModel(this);
  periodProxy->setSourceModel(periodModel_);
  periodProxy->setSortRole(Qt::UserRole);
  periodView_ = new QTableView(periods);
  periodView_->setObjectName("statsPeriodComparison");
  periodView_->setAccessibleName(tr("Taste comparison by completion period"));
  periodView_->setModel(periodProxy);
  periodView_->setSortingEnabled(true);
  periodView_->sortByColumn(5, Qt::DescendingOrder);
  periodView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  periodView_->setAlternatingRowColors(true);
  periodView_->setSelectionBehavior(QAbstractItemView::SelectRows);
  periodView_->verticalHeader()->hide();
  periodView_->horizontalHeader()->setStretchLastSection(true);
  periodView_->setMinimumHeight(280);
  periodView_->setColumnWidth(0, 160);
  for (int column : {1, 3}) periodView_->setColumnWidth(column, 115);
  for (int column : {2, 4}) periodView_->setColumnWidth(column, 80);
  periodView_->setColumnWidth(5, 115);
  periods->layout()->addWidget(periodView_);
  tasteBox->addWidget(periods);
  for (auto* box : {firstFrom_, firstTo_, secondFrom_, secondTo_})
    connect(box, &QSpinBox::valueChanged, this, [this] { invalidate(); });
  connect(periodDimension_, &QComboBox::currentIndexChanged, this, [this] { invalidate(); });
  connect(periodMinimum_, &QSpinBox::valueChanged, this, [this] { renderPeriodComparison(); });
  connect(periodView_, &QTableView::activated, this, [this](const QModelIndex& index) {
    if (!index.isValid()) return;
    drillSource_ = nullptr;
    drillDown(index.data(IdsRole).value<QList<int>>(), index.data(NoteRole).toString());
  });
  auto* taste = new StatsCard(tr("Find your favourites"),
                              tr("Explore genres, studios and formats by popularity in your list, "
                                 "personal rating or time spent."));
  auto* tasteControls = new QGridLayout;
  dimension_ = new QComboBox(taste);
  dimension_->setObjectName("statsDimension");
  dimension_->setAccessibleName(tr("Rank by category"));
  for (const auto& [name, d] : QList<QPair<QString, stats::Dimension>>{
           {tr("Genres"), stats::Dimension::Genre},
           {tr("Studios / authors"), stats::Dimension::Studio},
           {tr("Formats"), stats::Dimension::Format},
           {tr("Release years"), stats::Dimension::ReleaseYear},
           {tr("Release seasons"), stats::Dimension::ReleaseSeason},
           {tr("Series length"), stats::Dimension::Length}})
    dimension_->addItem(name, int(d));
  ranking_ = new QComboBox(taste);
  ranking_->setObjectName("statsRanking");
  ranking_->setAccessibleName(tr("Ranking metric"));
  ranking_->addItems(
      {tr("Most titles"), tr("Highest average score"), tr("Most episodes"), tr("Most watch time")});
  minimum_ = new QSpinBox(taste);
  minimum_->setObjectName("statsMinimum");
  minimum_->setRange(1, 100000);
  minimum_->setValue(5);
  minimum_->setPrefix(tr("At least "));
  minimum_->setSuffix(tr(" ratings"));
  minimum_->setAccessibleName(tr("Minimum rated titles for average score rankings"));
  minimum_->setToolTip(
      tr("Applied only when ranking by average score. This avoids one highly rated title "
         "dominating a ranking."));
  showAllGroups_ = new QPushButton(tr("Show all groups"), taste);
  showAllGroups_->setCheckable(true);
  tasteControls->addWidget(dimension_, 0, 0);
  tasteControls->addWidget(ranking_, 0, 1);
  tasteControls->addWidget(minimum_, 1, 0);
  tasteControls->addWidget(showAllGroups_, 1, 1);
  static_cast<QVBoxLayout*>(taste->layout())->addLayout(tasteControls);
  tasteNote_ = new QLabel(taste);
  tasteNote_->setWordWrap(true);
  taste->layout()->addWidget(tasteNote_);
  chart(taste, Taste, false, 15);
  charts_[Taste]->setObjectName("statsGroups");
  tasteBox->addWidget(taste);
  tasteBox->addStretch();
  connect(dimension_, &QComboBox::currentIndexChanged, this, [this] { renderGroups(); });
  connect(ranking_, &QComboBox::currentIndexChanged, this, [this] { renderGroups(); });
  connect(minimum_, &QSpinBox::valueChanged, this, [this] { renderGroups(); });
  connect(showAllGroups_, &QPushButton::toggled, this, [this] { renderGroups(); });

  auto* timelineBox = page(tr("Timeline"));
  timelineNote_ = new QLabel;
  timelineNote_->setWordWrap(true);
  timelineBox->addWidget(timelineNote_);
  auto* years =
      new StatsCard(tr("Completed through the years"),
                    tr("Titles completed each year, using the finish dates on your list."));
  timelineBox->addWidget(chart(years, Years, true));
  auto* months = new StatsCard(tr("Your year, month by month"), {});
  timelineYear_ = new QComboBox(months);
  timelineYear_->setObjectName("statsTimelineYear");
  timelineYear_->setAccessibleName(tr("Completion year"));
  months->layout()->addWidget(timelineYear_);
  timelineBox->addWidget(chart(months, Months, true));
  connect(timelineYear_, &QComboBox::currentIndexChanged, this, [this] { renderTimeline(); });
  timelineBox->addWidget(chart(new StatsCard(tr("The eras you explore"),
                                             tr("Release year of titles in this selection. This is "
                                                "separate from when you watched or read them.")),
                               Releases, true));
  auto* timelineGrid = new StatsGrid(400, 2);
  timelineBox->addWidget(timelineGrid);
  timelineGrid->add(
      chart(new StatsCard(tr("Short series or long commitments?"),
                          tr("Titles grouped by their total episode or chapter count.")),
            Length));
  timelineGrid->add(chart(
      new StatsCard(tr("How close are you to finishing?"),
                    tr("Watching, reading, on-hold titles and active repeats with known totals.")),
      Progress));
  timelineBox->addStretch();

  auto* activityBox = page(tr("Activity"));
  activityNote_ = new QLabel;
  activityNote_->setWordWrap(true);
  activityBox->addWidget(activityNote_);
  auto* daily =
      new StatsCard(tr("Progress over the last 30 days"),
                    tr("Episodes or chapters added through Taiga. Hover or focus a bar for the "
                       "exact date; corrections are excluded from added totals."));
  activityBox->addWidget(chart(daily, DailyActivity, true));
  charts_[DailyActivity]->setMinimumColumnWidth(20);
  activityBox->addWidget(
      chart(new StatsCard(
                tr("Progress over the last 12 months"),
                tr("Recorded increases, including progress made during rewatches and rereads.")),
            MonthlyActivity, true));
  for (int index : {DailyActivity, MonthlyActivity})
    charts_[index]->setEmptyText(
        tr("No progress has been recorded in Taiga yet. Progress edits will build this chart."));
  auto* activityCard = new StatsCard(tr("Progress and rating history"),
                                     tr("Exact before-and-after values for edits made in Taiga. "
                                        "Your global filters apply here too."));
  auto* activitySearch = activitySearch_ = new QLineEdit(activityCard);
  activitySearch->setObjectName("statsActivitySearch");
  activitySearch->setPlaceholderText(tr("Find a title in your activity…"));
  activitySearch->setAccessibleName(tr("Search local activity"));
  activitySearch->setClearButtonEnabled(true);
  activityCard->layout()->addWidget(activitySearch);
  activityModel_ = new StatsActivityModel(this);
  auto* activityProxy = new QSortFilterProxyModel(this);
  activityProxy->setSourceModel(activityModel_);
  activityProxy->setSortRole(Qt::UserRole);
  activityProxy->setFilterKeyColumn(1);
  activityProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
  activityView_ = new QTableView(activityCard);
  activityView_->setObjectName("statsActivity");
  activityView_->setAccessibleName(tr("Recorded progress and score changes"));
  activityView_->setModel(activityProxy);
  activityView_->setSortingEnabled(true);
  activityView_->sortByColumn(0, Qt::DescendingOrder);
  activityView_->setSelectionBehavior(QAbstractItemView::SelectRows);
  activityView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  activityView_->setAlternatingRowColors(true);
  activityView_->verticalHeader()->hide();
  activityView_->horizontalHeader()->setStretchLastSection(true);
  activityView_->setColumnWidth(0, 155);
  activityView_->setColumnWidth(1, 310);
  activityView_->setColumnWidth(2, 145);
  activityView_->setMinimumHeight(300);
  activityCard->layout()->addWidget(activityView_);
  activityBox->addWidget(activityCard);
  activityBox->addStretch();
  connect(activitySearch, &QLineEdit::textChanged, activityProxy,
          &QSortFilterProxyModel::setFilterFixedString);

  auto* library = new QWidget(tabs_);
  auto* libraryBox = new QVBoxLayout(library);
  libraryBox->setContentsMargins(20, 16, 20, 16);
  libraryBox->setSpacing(12);
  selection_ = new QLabel(library);
  selection_->setWordWrap(true);
  libraryBox->addWidget(selection_);
  auto* titleBar = new QHBoxLayout;
  auto* back = backToChart_ = new QPushButton(tr("Back to chart"), library);
  back->setObjectName("statsBackToChart");
  back->setEnabled(false);
  titleBar->addWidget(back);
  search_ = new QLineEdit(library);
  search_->setObjectName("statsTitleSearch");
  search_->setPlaceholderText(tr("Search these titles…"));
  search_->setClearButtonEnabled(true);
  search_->setAccessibleName(tr("Search statistics titles"));
  auto* all = new QPushButton(tr("All filtered titles"), library);
  all->setObjectName("statsAllTitles");
  auto* exportButton = new QPushButton(tr("Export CSV…"), library);
  titleBar->addWidget(search_, 1);
  titleBar->addWidget(all);
  titleBar->addWidget(exportButton);
  libraryBox->addLayout(titleBar);
  titlesEmpty_ = new QLabel(library);
  titlesEmpty_->setObjectName("statsTitlesEmpty");
  titlesEmpty_->setWordWrap(true);
  titlesEmpty_->hide();
  libraryBox->addWidget(titlesEmpty_);
  titlesModel_ = new StatsTitlesModel(this);
  titlesProxy_ = new QSortFilterProxyModel(this);
  titlesProxy_->setSourceModel(titlesModel_);
  titlesProxy_->setSortRole(Qt::UserRole);
  titlesProxy_->setSortCaseSensitivity(Qt::CaseInsensitive);
  titlesProxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
  titlesProxy_->setFilterKeyColumn(0);
  titlesView_ = new QTableView(library);
  titlesView_->setObjectName("statsTitles");
  titlesView_->setAccessibleName(tr("Titles contributing to the selected statistic"));
  titlesView_->setModel(titlesProxy_);
  titlesView_->setSortingEnabled(true);
  titlesView_->sortByColumn(0, Qt::AscendingOrder);
  titlesView_->setSelectionBehavior(QAbstractItemView::SelectRows);
  titlesView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  titlesView_->setAlternatingRowColors(true);
  titlesView_->verticalHeader()->hide();
  titlesView_->horizontalHeader()->resizeSection(0, 300);
  titlesView_->horizontalHeader()->setStretchLastSection(true);
  libraryBox->addWidget(titlesView_);
  tabs_->addTab(library, tr("Titles"));
  connect(search_, &QLineEdit::textChanged, titlesProxy_,
          &QSortFilterProxyModel::setFilterFixedString);
  connect(titlesProxy_, &QAbstractItemModel::modelReset, this, &StatsWidget::updateTitleCount);
  connect(titlesProxy_, &QAbstractItemModel::rowsInserted, this, &StatsWidget::updateTitleCount);
  connect(titlesProxy_, &QAbstractItemModel::rowsRemoved, this, &StatsWidget::updateTitleCount);
  connect(search_, &QLineEdit::textChanged, this, &StatsWidget::updateTitleCount);
  connect(all, &QPushButton::clicked, this, [this] {
    search_->clear();
    showTitles();
  });
  const auto returnToChart = [this] {
    if (tabs_->currentIndex() != kTitlesTab || !drilled_) return;
    tabs_->setCurrentIndex(previousTab_);
    if (drillSource_)
      drillSource_->setFocus();
    else if (previousTab_ == 2)
      periodView_->setFocus();
  };
  connect(back, &QPushButton::clicked, this, returnToChart);
  auto* find = new QAction(this);
  find->setShortcut(QKeySequence::Find);
  find->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  addAction(find);
  connect(find, &QAction::triggered, this, [this] {
    if (tabs_->currentIndex() == 4) {
      activitySearch_->setFocus();
      activitySearch_->selectAll();
    } else {
      tabs_->setCurrentIndex(kTitlesTab);
      search_->setFocus();
      search_->selectAll();
    }
  });
  auto* escape = new QAction(this);
  escape->setShortcut(QKeySequence(Qt::Key_Escape));
  escape->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  addAction(escape);
  connect(escape, &QAction::triggered, this, returnToChart);
  connect(exportButton, &QPushButton::clicked, this, [this] {
    const auto path =
        QFileDialog::getSaveFileName(this, tr("Export statistics titles"), {}, tr("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    const auto quote = [](QString text) {
      text.replace('"', QStringLiteral("\"\""));
      return '"' + text + '"';
    };
    QString csv;
    for (int row = -1; row < titlesProxy_->rowCount(); ++row) {
      QStringList fields;
      for (int c = 0; c < titlesProxy_->columnCount(); ++c)
        fields.append(quote(row < 0 ? titlesProxy_->headerData(c, Qt::Horizontal).toString()
                                    : titlesProxy_->index(row, c).data().toString()));
      csv += fields.join(',') + '\n';
    }
    const auto bytes = csv.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
      QMessageBox::warning(this, tr("Export failed"),
                           tr("Could not save the CSV file: %1").arg(file.errorString()));
    else
      selection_->setText(tr("Exported %1 titles to %2.").arg(titlesProxy_->rowCount()).arg(path));
  });
  const auto openTitle = [this](const QModelIndex& index) {
    const int id = index.data(Qt::UserRole + 1).toInt();
    if (!kind_->currentIndex()) {
      if (const auto* item = anime::db.item(id))
        MediaDialog::show(this, MediaDialogPage::Details, *item);
    } else
      QDesktopServices::openUrl(QUrl(QStringLiteral("https://myanimelist.net/manga/%1").arg(id)));
  };
  connect(titlesView_, &QTableView::activated, this, openTitle);
  connect(activityView_, &QTableView::activated, this, openTitle);
  for (int i = 0; i < tabs_->count(); ++i) {
    auto* action = new QAction(this);
    action->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
    action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(action);
    connect(action, &QAction::triggered, this, [this, i] { tabs_->setCurrentIndex(i); });
  }
  debounce_.setSingleShot(true);
  debounce_.setInterval(200);
  connect(&debounce_, &QTimer::timeout, this, &StatsWidget::refresh);
  for (auto signal : {&anime::Database::itemUpdated, &anime::Database::entryUpdated,
                      &anime::Database::entryDeleted})
    connect(&anime::db, signal, this, [this] { invalidate(); });
  connect(&anime::db, &anime::Database::itemDeleted, this, [this] { invalidate(); });
  connect(&manga::library, &manga::Library::reset, this, [this] { invalidate(); });
  connect(&manga::library, &manga::Library::changed, this, [this] { invalidate(); });
  connect(&taiga::accounts, &taiga::Accounts::authenticationChanged, this,
          [this] { invalidate(); });
  connect(&stats::activity, &stats::ActivityLog::changed, this, [this] { invalidate(); });
  updateSavedViews();
  connect(savedViews_, &QComboBox::activated, this, [this](int index) {
    if (index > 0) restoreView(QJsonObject::fromVariantMap(savedViews_->currentData().toMap()));
    updateView_->setEnabled(index > 0);
    deleteView_->setEnabled(index > 0);
  });
  connect(saveViewButton, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const auto name = QInputDialog::getText(this, tr("Save statistics view"), tr("View name"),
                                            QLineEdit::Normal, {}, &ok)
                          .trimmed();
    if (!ok) return;
    bool exists = false;
    for (int i = 1; i < savedViews_->count(); ++i) exists |= savedViews_->itemText(i) == name;
    if (exists &&
        QMessageBox::question(this, tr("Replace saved view?"),
                              tr("Replace the saved view \"%1\"?").arg(name)) != QMessageBox::Yes)
      return;
    saveView(name);
  });
  connect(updateView_, &QPushButton::clicked, this,
          [this] { saveView(savedViews_->currentText()); });
  connect(deleteView_, &QPushButton::clicked, this, [this] {
    const auto name = savedViews_->currentText();
    if (savedViews_->currentIndex() <= 0 ||
        QMessageBox::question(
            this, tr("Delete saved view?"),
            tr("Delete \"%1\"? Your list and account data are kept.").arg(name)) !=
            QMessageBox::Yes)
      return;
    QJsonArray remaining;
    for (const auto& item : taiga::session.statisticsViews())
      if (item.toObject()["name"].toString() != name) remaining.append(item);
    const bool saved = taiga::session.setStatisticsViews(remaining);
    viewNotice_->setText(
        saved ? tr("Deleted view \"%1\".").arg(name)
              : tr("Could not delete the saved view. Check that Taiga's data folder is writable."));
    viewNotice_->show();
    if (saved) updateSavedViews();
  });
  restoreView(taiga::session.statisticsState());
  connect(qApp, &QCoreApplication::aboutToQuit, this, &StatsWidget::saveState);
  manga::library.init();
}

QJsonObject StatsWidget::viewState() const {
  QJsonObject state{
      {"version", 1},
      {"kind", kind_->currentIndex()},
      {"tab", tabs_->currentIndex()},
      {"filtersVisible", filterToggle_->isChecked()},
      {"dimension", dimension_->currentData().toInt()},
      {"ranking", ranking_->currentIndex()},
      {"minimum", minimum_->value()},
      {"allGroups", showAllGroups_->isChecked()},
      {"timelineYear", timelineYear_->currentData().toString()},
      {"periodDimension", periodDimension_->currentData().toInt()},
      {"firstFrom", firstFrom_->value()},
      {"firstTo", firstTo_->value()},
      {"secondFrom", secondFrom_->value()},
      {"secondTo", secondTo_->value()},
      {"periodMinimum", periodMinimum_->value()},
      {"titleSearch", search_->text()},
      {"activitySearch", activitySearch_->text()},
      {"titlesHeader",
       QString::fromLatin1(titlesView_->horizontalHeader()->saveState().toBase64())},
      {"activityHeader",
       QString::fromLatin1(activityView_->horizontalHeader()->saveState().toBase64())},
      {"periodHeader",
       QString::fromLatin1(periodView_->horizontalHeader()->saveState().toBase64())}};
  const QList<QComboBox*> filters{status_, format_, year_, genre_, studio_};
  const QStringList names{"status", "format", "year", "genre", "studio"};
  for (int i = 0; i < filters.size(); ++i) state[names[i]] = filters[i]->currentData().toString();
  return state;
}

void StatsWidget::saveState() const {
  if (!taiga::session.setStatisticsState(viewState()))
    qWarning("Could not save statistics view state.");
}

void StatsWidget::hideEvent(QHideEvent* event) {
  saveState();
  PageWidget::hideEvent(event);
}

void StatsWidget::restoreView(const QJsonObject& state) {
  if (state["version"].toInt() != 1) return;
  const QSignalBlocker kindBlock(kind_);
  kind_->setCurrentIndex(std::clamp(state["kind"].toInt(), 0, 1));
  updateChoices(snapshot());
  const QList<QComboBox*> filters{status_, format_, year_, genre_, studio_};
  const QStringList names{"status", "format", "year", "genre", "studio"};
  for (int i = 0; i < filters.size(); ++i) {
    auto* box = filters[i];
    const QSignalBlocker block(box);
    const auto value = state[names[i]].toString();
    if (box->findData(value) < 0) box->addItem(tr("%1 (unavailable)").arg(value), value);
    box->setCurrentIndex(box->findData(value));
  }
  const auto restoreCombo = [&](QComboBox* box, const char* key) {
    const QSignalBlocker block(box);
    const int index = box->findData(state[key].toInt());
    box->setCurrentIndex(std::max(0, index));
  };
  restoreCombo(dimension_, "dimension");
  restoreCombo(periodDimension_, "periodDimension");
  ranking_->setCurrentIndex(std::clamp(state["ranking"].toInt(), 0, kind_->currentIndex() ? 2 : 3));
  minimum_->setValue(state["minimum"].toInt(5));
  periodMinimum_->setValue(state["periodMinimum"].toInt(3));
  for (const auto& [box, key] : QList<QPair<QSpinBox*, const char*>>{{firstFrom_, "firstFrom"},
                                                                     {firstTo_, "firstTo"},
                                                                     {secondFrom_, "secondFrom"},
                                                                     {secondTo_, "secondTo"}}) {
    const QSignalBlocker block(box);
    box->setValue(state[key].toInt(box->value()));
  }
  showAllGroups_->setChecked(state["allGroups"].toBool());
  filterToggle_->setChecked(state["filtersVisible"].toBool());
  search_->setText(state["titleSearch"].toString());
  activitySearch_->setText(state["activitySearch"].toString());
  restoredTimelineYear_ = state["timelineYear"].toString();
  for (const auto& [view, key] :
       QList<QPair<QTableView*, const char*>>{{titlesView_, "titlesHeader"},
                                              {activityView_, "activityHeader"},
                                              {periodView_, "periodHeader"}}) {
    const auto header = QByteArray::fromBase64(state[key].toString().toLatin1());
    if (!header.isEmpty()) view->horizontalHeader()->restoreState(header);
    view->sortByColumn(view->horizontalHeader()->sortIndicatorSection(),
                       view->horizontalHeader()->sortIndicatorOrder());
  }
  drillSource_ = nullptr;
  drilled_ = false;
  drillIds_.clear();
  drillLabel_.clear();
  backToChart_->setEnabled(false);
  tabs_->setCurrentIndex(std::clamp(state["tab"].toInt(), 0, tabs_->count() - 1));
  viewNotice_->hide();
  invalidate();
}

void StatsWidget::updateSavedViews(const QString& name) {
  const QSignalBlocker block(savedViews_);
  savedViews_->clear();
  savedViews_->addItem(tr("Saved views"));
  for (const auto& value : taiga::session.statisticsViews()) {
    const auto entry = value.toObject();
    const auto title = entry["name"].toString().trimmed();
    const auto state = entry["state"].toObject();
    if (!title.isEmpty() && title.size() <= 120 && state["version"].toInt() == 1)
      savedViews_->addItem(title, state.toVariantMap());
  }
  int selected = 0;
  for (int i = 1; i < savedViews_->count(); ++i)
    if (savedViews_->itemText(i) == name) selected = i;
  savedViews_->setCurrentIndex(selected);
  updateView_->setEnabled(savedViews_->currentIndex() > 0);
  deleteView_->setEnabled(savedViews_->currentIndex() > 0);
}

bool StatsWidget::saveView(const QString& name) {
  const auto trimmed = name.trimmed();
  if (trimmed.isEmpty() || trimmed.size() > 120) {
    viewNotice_->setText(tr("Use a view name between 1 and 120 characters."));
    viewNotice_->show();
    return false;
  }
  auto views = taiga::session.statisticsViews();
  const QJsonObject entry{{"name", trimmed}, {"state", viewState()}};
  int existing = -1;
  for (int i = 0; i < views.size(); ++i)
    if (views[i].toObject()["name"].toString() == trimmed) existing = i;
  if (existing >= 0)
    views[existing] = entry;
  else
    views.append(entry);
  const bool saved = taiga::session.setStatisticsViews(views);
  viewNotice_->setText(
      saved ? tr("Saved view \"%1\".").arg(trimmed)
            : tr("Could not save the view. Check that Taiga's data folder is writable."));
  viewNotice_->show();
  if (saved) updateSavedViews(trimmed);
  return saved;
}

QList<stats::Record> StatsWidget::snapshot() const {
  QList<stats::Record> records;
  if (!kind_->currentIndex()) {
    records.reserve(anime::db.entries().size());
    for (const auto& entry : anime::db.entries()) {
      if (entry.pending_delete || entry.status == anime::list::Status::NotInList) continue;
      stats::Record r;
      r.id = entry.anime_id;
      r.status = formatListStatus(entry.status);
      r.score = entry.score / 10.0;
      r.progress = entry.watched_episodes;
      r.repeats = entry.rewatched_times;
      r.repeating = entry.rewatching;
      r.started = exactDate(entry.date_started);
      r.completed = exactDate(entry.date_completed);
      r.completionYear = yearKey(entry.date_completed);
      r.completionMonth = monthKey(entry.date_completed);
      r.updated = entry.last_updated > 0 ? QDateTime::fromSecsSinceEpoch(entry.last_updated).date()
                                         : QDate{};
      if (const auto* item = anime::db.item(entry.anime_id)) {
        r.title = QString::fromStdString(anime::preferredTitle(*item));
        r.format = formatType(item->type);
        r.genres = strings(item->genres);
        r.studios = strings(item->studios);
        r.releaseYear = yearKey(item->date_started);
        if (item->date_started.month() >= 1 && item->date_started.month() <= 12 &&
            !r.releaseYear.isEmpty()) {
          const QStringList seasons{QStringLiteral("Winter"), QStringLiteral("Spring"),
                                    QStringLiteral("Summer"), QStringLiteral("Fall")};
          r.releaseSeason = r.releaseYear + ' ' + seasons[(item->date_started.month() - 1) / 3];
        }
        r.total = item->episode_count;
        r.communityScore = item->score;
        r.minutesPerEpisode = anime::estimateEpisodeLength(*item);
        r.estimatedDuration = item->episode_length <= 0;
      } else
        r.title = tr("Unknown title #%1").arg(r.id);
      records.append(r);
    }
  } else {
    records.reserve(manga::library.entries().size());
    for (const auto& e : manga::library.entries()) {
      stats::Record r;
      r.id = e.id;
      r.title = e.title;
      r.status = mangaStatus(e.status);
      r.format = e.type;
      r.format.replace('_', ' ');
      r.genres = e.genres;
      r.studios = e.authors;
      const auto released = fuzzyDate(e.startDate), completed = fuzzyDate(e.finishedReading);
      r.releaseYear = yearKey(released);
      if (released.month() >= 1 && released.month() <= 12 && !r.releaseYear.isEmpty()) {
        const QStringList seasons{"Winter", "Spring", "Summer", "Fall"};
        r.releaseSeason = r.releaseYear + ' ' + seasons[(released.month() - 1) / 3];
      }
      r.completionYear = yearKey(completed);
      r.completionMonth = monthKey(completed);
      r.started = QDate::fromString(e.startedReading, Qt::ISODate);
      r.completed = QDate::fromString(e.finishedReading, Qt::ISODate);
      r.updated = QDateTime::fromString(e.updatedAt, Qt::ISODate).date();
      r.score = e.score;
      r.communityScore = e.mean;
      r.progress = e.chaptersRead;
      r.total = e.chapters;
      r.secondaryProgress = e.volumesRead;
      r.repeats = e.timesReread;
      r.repeating = e.rereading;
      records.append(r);
    }
  }
  return records;
}

void StatsWidget::updateChoices(const QList<stats::Record>& records) {
  QList<QSet<QString>> values(5);
  for (const auto& r : records) {
    values[0].insert(r.status);
    values[1].insert(r.format);
    values[2].insert(r.releaseYear);
    for (const auto& g : r.genres) values[3].insert(g);
    for (const auto& studio : r.studios) values[4].insert(studio);
  }
  const QList<QComboBox*> boxes{status_, format_, year_, genre_, studio_};
  for (int i = 0; i < boxes.size(); ++i) {
    auto* box = boxes[i];
    const auto old = box->currentData().toString();
    QSignalBlocker block(box);
    auto choices = values[i].values();
    choices.removeAll(QString{});
    std::sort(choices.begin(), choices.end());
    box->clear();
    box->addItem(tr("All"), QString{});
    for (const auto& text : choices) box->addItem(text, text);
    if (!old.isEmpty() && box->findData(old) < 0)
      box->addItem(tr("%1 (unavailable)").arg(old), old);
    box->setCurrentIndex(std::max(0, box->findData(old)));
  }
}

void StatsWidget::invalidate() {
  dirty_ = true;
  ++revision_;
  titlesView_->setEnabled(false);
  activityView_->setEnabled(false);
  periodView_->setEnabled(false);
  for (auto* chart : charts_) chart->setEnabled(false);
  if (isVisible()) debounce_.start();
}
void StatsWidget::showEvent(QShowEvent* event) {
  PageWidget::showEvent(event);
  if (dirty_) refresh();
}
void StatsWidget::refresh() {
  if (!isVisible() || !dirty_ || calculating_) return;
  auto records = snapshot();
  updateChoices(records);
  const stats::Filter filter{status_->currentData().toString(), format_->currentData().toString(),
                             year_->currentData().toString(), genre_->currentData().toString(),
                             studio_->currentData().toString()};
  const auto revision = revision_;
  calculating_ = true;
  titlesView_->setEnabled(false);
  for (auto* chart : charts_) chart->setEnabled(false);
  overview_->setText(tr("Calculating statistics for %1 saved titles…").arg(count(records.size())));
  auto changes = stats::activity.records(kind_->currentIndex() ? stats::MediaKind::Manga
                                                               : stats::MediaKind::Anime);
  const stats::Period first{firstFrom_->value(), firstTo_->value()},
      second{secondFrom_->value(), secondTo_->value()};
  const auto dimension = stats::Dimension(periodDimension_->currentData().toInt());
  QtConcurrent::run([records = std::move(records), changes = std::move(changes), filter, first,
                     second, dimension] {
    auto summary = stats::calculate(records, filter);
    QSet<int> ids;
    for (const auto& record : summary.records) ids.insert(record.id);
    auto activity = stats::calculateActivity(changes, ids);
    auto periods = stats::comparePeriods(summary.records, first, second, dimension);
    return StatsResult{std::move(summary), std::move(activity), std::move(periods)};
  }).then(this, [this, revision](StatsResult result) {
    calculating_ = false;
    if (revision != revision_) {
      debounce_.start();
      return;
    }
    dirty_ = false;
    summary_ = std::move(result.summary);
    activitySummary_ = std::move(result.activity);
    periodComparison_ = std::move(result.periods);
    render();
    titlesView_->setEnabled(true);
    activityView_->setEnabled(true);
    periodView_->setEnabled(true);
    for (auto* chart : charts_) chart->setEnabled(true);
  });
}

void StatsWidget::render() {
  const auto& s = summary_;
  const bool manga = kind_->currentIndex();
  const auto unit = manga ? tr("chapters") : tr("episodes");
  const auto service = manga ? std::string("myanimelist") : taiga::settings.service();
  const auto username = QString::fromStdString(taiga::accounts.serviceUsername(service));
  QStringList filters;
  for (auto* box : {status_, format_, year_, genre_, studio_})
    if (!box->currentData().toString().isEmpty()) filters.append(box->currentText());
  const auto account =
      service == "myanimelist" ? tr("MyAnimeList") : QString::fromStdString(service);
  overview_->setText(
      s.records.isEmpty()
          ? tr("No titles in this selection. Clear filters or synchronize your list.")
          : tr("%1%2  /  %3")
                .arg(account, username.isEmpty() ? QString{} : " · " + username,
                     filters.isEmpty()
                         ? tr("Your entire %1 list").arg(manga ? tr("manga") : tr("anime"))
                         : filters.join(", ")));
  values_[0]->setText(count(s.records.size()));
  notes_[0]->setText(tr("Titles in your list\n%1 completed, %2 dropped")
                         .arg(count(s.completed), count(s.dropped)));
  values_[1]->setText(count(s.units));
  notes_[1]->setText(tr("%1 %2\n%3 from repeats")
                         .arg(manga ? tr("Chapters") : tr("Episodes"),
                              manga ? tr("read") : tr("watched"), count(s.repeatUnits)));
  values_[2]->setText(manga ? count(s.secondaryUnits)
                            : tr("%1 d").arg(QLocale().toString(s.seconds / 86400.0, 'f', 1)));
  notes_[2]->setText(manga ? tr("Volumes read\nNo reading-time estimate")
                           : tr("Estimated watch time\n%1 hours").arg(hours(s.seconds)));
  values_[3]->setText(s.scored ? number(s.mean) : QStringLiteral("—"));
  notes_[3]->setText(tr("Mean score out of 10\n%1 rated, %2 unrated")
                         .arg(count(s.scored), count(s.records.size() - s.scored)));
  backlog_->setText(tr("%1 known %2 left%3. %4 unfinished titles unchanged for 180+ days. "
                       "Completed and dropped titles are excluded unless repeating.")
                        .arg(count(s.remainingUnits), unit,
                             manga ? QString{} : tr(" (%1 hours)").arg(hours(s.remainingSeconds)),
                             count(s.stale)));
  repeats_->setText(tr("%1 completed repeats and %2 active across %3 titles, adding %4 %5.")
                        .arg(count(s.repeatCount), count(s.activeRepeats), count(s.repeatedTitles),
                             count(s.repeatUnits), unit));
  QStringList modes;
  for (const auto mode : s.modes) modes.append(QLocale().toString(mode, 'f', 1));
  scores_->setText(
      s.scored ? tr("Mean %1    Median %2    Standard deviation %3\nMost frequent score: %4\n%5 "
                    "rated titles; %6 unrated titles are excluded. %7-weighted mean: %8.")
                     .arg(number(s.mean), number(s.median), number(s.deviation), modes.join(", "),
                          count(s.scored), count(s.records.size() - s.scored),
                          manga ? tr("Chapter") : tr("Episode"),
                          s.ratedUnits ? number(s.weightedMean) : tr("Unavailable"))
               : tr("No rated titles in this selection. Add scores on your list to explore your "
                    "rating habits."));
  comparison_->setText(
      s.compared ? tr("%1 comparable titles. On average, you rate %2 points %3 MAL.\n%4 higher, %5 "
                      "similar (within 0.5), %6 lower. Mean absolute gap: %7.\nMatched-title "
                      "averages: you %8 / 10, MAL %9 / 10.")
                       .arg(count(s.compared), number(std::abs(s.meanDifference)),
                            s.meanDifference < 0 ? tr("below") : tr("above"), count(s.higher),
                            count(s.similar), count(s.lower), number(s.meanAbsoluteDifference),
                            number(s.communityMean + s.meanDifference), number(s.communityMean))
                 : tr("No comparable scores. Both your score and MAL’s mean are required."));
  coverage_->setText(
      tr("These statistics use your saved account list. Synchronize the list to update it.\n"
         "Unrated titles never count as zero in score averages. Genre and studio totals overlap "
         "when a title has several.\n"
         "%1 titles have unknown totals. %2 completed titles have no completion year.\n%3\n"
         "Completion timelines use your recorded finish dates, not daily episode activity or past "
         "score changes.")
          .arg(count(s.unknownTotals), count(s.unknownCompletionDates),
               manga
                   ? tr("Chapters and recorded rereads are counted; reading time is not inferred.")
                   : tr("Watch time includes recorded rewatches. %1 durations are estimated; %2 "
                        "are unavailable. Repeats with unknown totals add no estimated episodes.")
                         .arg(count(s.estimatedDuration), count(s.unknownDuration))));
  ranking_->setItemText(2, manga ? tr("Most chapters") : tr("Most episodes"));
  auto* rankingModel = qobject_cast<QStandardItemModel*>(ranking_->model());
  rankingModel->item(3)->setEnabled(!manga);
  if (manga && ranking_->currentIndex() == 3) {
    QSignalBlocker b(ranking_);
    ranking_->setCurrentIndex(2);
  }
  dimension_->setItemText(1, manga ? tr("Authors") : tr("Studios"));

  const auto groupRows = [&](stats::Dimension dimension, int limit) {
    auto groups = s.groups.value(dimension);
    std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) {
      return a.ids.size() != b.ids.size() ? a.ids.size() > b.ids.size() : a.name < b.name;
    });
    QList<ChartRow> rows;
    for (const auto& g : groups) {
      if (rows.size() == limit) break;
      rows.append(
          {g.name, count(g.ids.size()),
           tr("%1% of this list   Average %2 / 10")
               .arg(QLocale().toString(
                        s.records.isEmpty() ? 0 : 100.0 * g.ids.size() / s.records.size(), 'f', 1),
                    g.scored ? number(g.mean()) : tr("unrated")),
           double(g.ids.size()), g.ids});
    }
    return rows;
  };
  charts_[Status]->setRows(groupRows(stats::Dimension::Status, 6));
  charts_[Genres]->setRows(groupRows(stats::Dimension::Genre, 6));
  charts_[Formats]->setRows(groupRows(stats::Dimension::Format, 6));
  QMap<QString, stats::Group> scores;
  for (const auto& g : s.groups.value(stats::Dimension::Score)) scores.insert(g.name, g);
  QList<ChartRow> scoreRows;
  if (s.scored)
    for (int score = 1; score <= 10; ++score) {
      const auto g = scores.value(QString::number(score, 'f', 1));
      scoreRows.append({QString::number(score), count(g.ids.size()),
                        tr("Titles you rated %1 / 10").arg(score), double(g.ids.size()), g.ids});
    }
  charts_[OverviewScore]->setRows(scoreRows);
  charts_[RatingsScore]->setRows(scoreRows);
  QMap<QString, stats::Group> differences;
  for (const auto& g : s.groups.value(stats::Dimension::ScoreDifference))
    differences.insert(g.name, g);
  QList<ChartRow> differenceRows;
  if (s.compared)
    for (int difference = -4; difference <= 4; ++difference) {
      const auto g = differences.value(QString::number(difference));
      const auto label = difference == -4  ? QStringLiteral("≤−4")
                         : difference == 4 ? QStringLiteral("≥+4")
                         : difference > 0  ? "+" + QString::number(difference)
                                           : QString::number(difference);
      differenceRows.append({label, count(g.ids.size()), tr("Your score minus MAL’s mean, rounded"),
                             double(g.ids.size()), g.ids});
    }
  charts_[Difference]->setRows(differenceRows);
  QList<stats::Record> unfinished, repeated, above, below;
  for (const auto& r : s.records) {
    if ((r.repeating || (r.status != "Completed" && r.status != "Dropped")) && r.total > r.progress)
      unfinished.append(r);
    if (r.repeats > 0 || r.repeating) repeated.append(r);
    if (r.score > 0 && r.communityScore > 0) {
      if (r.score > r.communityScore) above.append(r);
      if (r.score < r.communityScore) below.append(r);
    }
  }
  std::sort(unfinished.begin(), unfinished.end(), [](const auto& a, const auto& b) {
    return a.total - a.progress != b.total - b.progress
               ? a.total - a.progress > b.total - b.progress
               : a.title < b.title;
  });
  std::sort(repeated.begin(), repeated.end(), [](const auto& a, const auto& b) {
    return a.repeats != b.repeats ? a.repeats > b.repeats : a.title < b.title;
  });
  const auto byGap = [](const auto& a, const auto& b) {
    const double ad = std::abs(a.score - a.communityScore),
                 bd = std::abs(b.score - b.communityScore);
    return ad != bd ? ad > bd : a.title < b.title;
  };
  std::sort(above.begin(), above.end(), byGap);
  std::sort(below.begin(), below.end(), byGap);
  QList<ChartRow> backlogRows, repeatRows;
  for (const auto& r : unfinished.first(std::min(qsizetype(6), unfinished.size())))
    backlogRows.append(
        {r.title,
         tr("%1 left").arg(count(r.total - r.progress)),
         tr("%1   %2 / %3 %4").arg(r.status, count(r.progress), count(r.total), unit),
         double(r.total - r.progress),
         {r.id}});
  for (const auto& r : repeated.first(std::min(qsizetype(6), repeated.size())))
    repeatRows.append({r.title,
                       count(r.repeats) + (r.repeating ? tr(" + active") : QString{}),
                       r.repeating ? tr("Repeat in progress")
                                   : (manga ? tr("Recorded rereads") : tr("Recorded rewatches")),
                       double(r.repeats + r.repeating),
                       {r.id}});
  const auto disagreements = [](const QList<stats::Record>& records) {
    QList<ChartRow> rows;
    for (const auto& r : records.first(std::min(qsizetype(6), records.size()))) {
      const double difference = r.score - r.communityScore;
      rows.append({r.title,
                   (difference > 0 ? "+" : "") + number(difference),
                   StatsWidget::tr("You: %1 / 10   MAL: %2 / 10")
                       .arg(number(r.score), number(r.communityScore)),
                   std::abs(difference),
                   {r.id}});
    }
    return rows;
  };
  charts_[Backlog]->setRows(backlogRows);
  charts_[Repeats]->setRows(repeatRows);
  charts_[Above]->setRows(disagreements(above));
  charts_[Below]->setRows(disagreements(below));
  renderGroups();
  const QSignalBlocker timelineBlock(timelineYear_);
  const auto previousYear = restoredTimelineYear_.isEmpty()
                                ? timelineYear_->currentData().toString()
                                : restoredTimelineYear_;
  restoredTimelineYear_.clear();
  timelineYear_->clear();
  const auto years = s.groups.value(stats::Dimension::CompletionYear);
  for (auto it = years.crbegin(); it != years.crend(); ++it)
    if (it->name != "Unknown") timelineYear_->addItem(it->name, it->name);
  if (timelineYear_->count() == 0) timelineYear_->addItem(tr("No dated completions"), QString{});
  const int previousIndex = timelineYear_->findData(previousYear);
  if (previousIndex >= 0) {
    timelineYear_->setCurrentIndex(previousIndex);
  } else if (!previousYear.isEmpty()) {
    timelineYear_->addItem(tr("%1 (no completions)").arg(previousYear), previousYear);
    timelineYear_->setCurrentIndex(timelineYear_->count() - 1);
  }
  renderTimeline();
  renderActivity();
  renderPeriodComparison();
  showTitles(drillIds_, drillLabel_, !drilled_);
}

void StatsWidget::renderPeriodComparison() {
  const auto& result = periodComparison_;
  periodModel_->removeRows(0, periodModel_->rowCount());
  periodView_->setVisible(result.valid && !result.groups.isEmpty());
  if (!result.valid) {
    periodNote_->setText(
        tr("Choose two non-overlapping periods. Each start year must be no later "
           "than its end year."));
    return;
  }
  const auto firstLabel = tr("A: %1-%2").arg(firstFrom_->value()).arg(firstTo_->value());
  const auto secondLabel = tr("B: %1-%2").arg(secondFrom_->value()).arg(secondTo_->value());
  const auto mean = [](const stats::Group& group) {
    return group.scored ? number(group.mean()) : StatsWidget::tr("Unrated");
  };
  QString note = tr("%1: %2 completed, %3 rated, mean %4. %5: %6 completed, %7 rated, mean %8.")
                     .arg(firstLabel, count(result.first.ids.size()), count(result.first.scored),
                          mean(result.first), secondLabel, count(result.second.ids.size()),
                          count(result.second.scored), mean(result.second));
  if (result.first.scored >= periodMinimum_->value() &&
      result.second.scored >= periodMinimum_->value())
    note += tr(" Mean score change: %1.").arg(number(result.second.mean() - result.first.mean()));
  note +=
      tr(" %1 completed titles have no usable finish year and are excluded. Global filters apply.")
          .arg(count(result.missingDates));
  if (result.first.ids.isEmpty() || result.second.ids.isEmpty())
    note += tr(" No completed titles in one or both periods. Adjust the years or clear filters.");
  else
    note += tr(
        " Share changes are percentage points, so different period sizes remain comparable. "
        "Genres and studios can overlap. Double-click or press Enter on A or B to see its titles.");
  periodNote_->setText(note);
  const auto signedNumber = [](double value) {
    return (value > 0 ? QStringLiteral("+") : QString{}) + number(value);
  };
  for (const auto& group : result.groups) {
    const double firstShare =
        result.first.ids.isEmpty() ? 0 : 100.0 * group.first.ids.size() / result.first.ids.size();
    const double secondShare = result.second.ids.isEmpty()
                                   ? 0
                                   : 100.0 * group.second.ids.size() / result.second.ids.size();
    const bool comparable = group.first.scored >= periodMinimum_->value() &&
                            group.second.scored >= periodMinimum_->value();
    const bool sharesComparable = !result.first.ids.isEmpty() && !result.second.ids.isEmpty();
    const double delta = group.second.mean() - group.first.mean();
    const QStringList text{
        group.name,
        tr("%1 / %2%").arg(count(group.first.ids.size()), number(firstShare)),
        mean(group.first),
        tr("%1 / %2%").arg(count(group.second.ids.size()), number(secondShare)),
        mean(group.second),
        sharesComparable ? signedNumber(secondShare - firstShare) + tr(" pp") : QStringLiteral("—"),
        comparable ? signedNumber(delta) : QStringLiteral("—")};
    QList<int> combined = group.first.ids;
    combined.append(group.second.ids);
    const QString details =
        tr("%1\n%2: %3 titles, %4 rated, mean %5\n%6: %7 titles, %8 rated, mean %9\n"
           "Score changes require at least %10 ratings in each period.")
            .arg(group.name, firstLabel, count(group.first.ids.size()), count(group.first.scored),
                 mean(group.first), secondLabel, count(group.second.ids.size()),
                 count(group.second.scored), mean(group.second), count(periodMinimum_->value()));
    QList<QStandardItem*> items;
    for (int column = 0; column < text.size(); ++column) {
      auto* item = new QStandardItem(text[column]);
      const bool first = column == 1 || column == 2, second = column == 3 || column == 4;
      item->setData(QVariant::fromValue(first    ? group.first.ids
                                        : second ? group.second.ids
                                                 : combined),
                    IdsRole);
      item->setData(tr("%1 / %2").arg(group.name, first    ? firstLabel
                                                  : second ? secondLabel
                                                           : firstLabel + ", " + secondLabel),
                    NoteRole);
      item->setToolTip(details);
      item->setData(text[column] + ". " + details, Qt::AccessibleTextRole);
      if (column == 0)
        item->setData(group.name, Qt::UserRole);
      else {
        const double amount = column == 1   ? group.first.ids.size()
                              : column == 2 ? (group.first.scored ? group.first.mean() : -1000)
                              : column == 3 ? group.second.ids.size()
                              : column == 4 ? (group.second.scored ? group.second.mean() : -1000)
                              : column == 5 ? (sharesComparable ? secondShare - firstShare : -1000)
                                            : (comparable ? delta : -1000);
        item->setData(amount, Qt::UserRole);
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      }
      items.append(item);
    }
    periodModel_->appendRow(items);
  }
}

void StatsWidget::renderGroups() {
  if (!charts_[Taste]) return;
  const bool manga = kind_->currentIndex();
  const auto dimension = static_cast<stats::Dimension>(dimension_->currentData().toInt());
  const int metric = ranking_->currentIndex();
  minimum_->setEnabled(metric == 1);
  auto groups = summary_.groups.value(dimension);
  if (metric == 1) groups.removeIf([this](const auto& g) { return g.scored < minimum_->value(); });
  const auto amount = [metric](const stats::Group& g) {
    switch (metric) {
      case 1:
        return g.mean();
      case 2:
        return double(g.units);
      case 3:
        return g.seconds / 3600.0;
      default:
        return double(g.ids.size());
    }
  };
  std::sort(groups.begin(), groups.end(), [&](const auto& a, const auto& b) {
    return amount(a) != amount(b) ? amount(a) > amount(b) : a.name < b.name;
  });
  const int size = int(groups.size());
  if (!showAllGroups_->isChecked() && groups.size() > 15) groups.resize(15);
  showAllGroups_->setVisible(size > 15);
  showAllGroups_->setText(showAllGroups_->isChecked() ? tr("Show top 15")
                                                      : tr("Show all %1 groups").arg(size));
  tasteNote_->setText(metric == 1 ? tr("Ranked by your mean score. Groups need at least %1 rated "
                                       "titles. Unrated titles are excluded from averages.")
                                        .arg(minimum_->value())
                                  : tr("%1 groups. Each bar opens its titles. Groups may overlap; "
                                       "time and progress include recorded repeats.")
                                        .arg(size));
  QList<ChartRow> rows;
  for (const auto& g : groups) {
    const auto value = metric == 1   ? number(g.mean()) + " / 10"
                       : metric == 3 ? hours(g.seconds) + tr(" h")
                                     : count(metric == 2 ? g.units : g.ids.size());
    rows.append({g.name, value,
                 tr("%1 titles   %2 rated   Average %3 / 10   %4 %5")
                     .arg(count(g.ids.size()), count(g.scored),
                          g.scored ? number(g.mean()) : QStringLiteral("—"), count(g.units),
                          manga ? tr("chapters") : tr("episodes")),
                 amount(g), g.ids});
  }
  charts_[Taste]->setRows(rows);
}

void StatsWidget::renderTimeline() {
  const auto& s = summary_;
  const auto columns = [&](stats::Dimension dimension) {
    QList<ChartRow> rows;
    for (const auto& g : s.groups.value(dimension)) {
      if (g.name == "Unknown") continue;
      rows.append({g.name, count(g.ids.size()),
                   tr("%1 titles, average %2 / 10")
                       .arg(count(g.ids.size()), g.scored ? number(g.mean()) : tr("unrated")),
                   double(g.ids.size()), g.ids});
    }
    return rows;
  };
  charts_[Years]->setRows(columns(stats::Dimension::CompletionYear));
  charts_[Releases]->setRows(columns(stats::Dimension::ReleaseYear));
  QMap<QString, stats::Group> months;
  for (const auto& g : s.groups.value(stats::Dimension::CompletionMonth)) months.insert(g.name, g);
  QList<ChartRow> monthRows;
  const auto year = timelineYear_->currentData().toString();
  if (!year.isEmpty())
    for (int month = 1; month <= 12; ++month) {
      const auto key = year + QStringLiteral("-%1").arg(month, 2, 10, QChar('0'));
      const auto g = months.value(key);
      monthRows.append({QLocale().monthName(month, QLocale::ShortFormat), count(g.ids.size()),
                        tr("Completed in %1").arg(key), double(g.ids.size()), g.ids});
    }
  charts_[Months]->setRows(monthRows);
  int withoutMonth = 0, unknownRelease = 0;
  for (const auto& r : s.records) {
    withoutMonth += r.status == "Completed" && r.completionMonth.isEmpty();
    unknownRelease += r.releaseYear.isEmpty();
  }
  timelineNote_->setText(
      tr("%1 completed titles. %2 have no completion year; %3 have no exact completion month.\n"
         "%4 titles have no release year. Missing dates are excluded from the corresponding "
         "charts.")
          .arg(count(s.completed), count(s.unknownCompletionDates), count(withoutMonth),
               count(unknownRelease)));
  QList<ChartRow> lengthRows, progressRows;
  QMap<QString, stats::Group> lengths;
  for (const auto& g : s.groups.value(stats::Dimension::Length)) lengths.insert(g.name, g);
  for (const auto& key :
       QStringList{"1", "2-6", "7-16", "17-28", "29-55", "56-100", "101+", "Unknown"}) {
    const auto g = lengths.value(key);
    if (g.ids.isEmpty()) continue;
    lengthRows.append({key, count(g.ids.size()),
                       tr("Average %1 / 10").arg(g.scored ? number(g.mean()) : tr("unrated")),
                       double(g.ids.size()), g.ids});
  }
  charts_[Length]->setRows(lengthRows);
  const QStringList labels{tr("Under 25%"), tr("25–49%"), tr("50–74%"), tr("75% or more")};
  for (const auto& g : s.groups.value(stats::Dimension::Progress)) {
    progressRows.append({labels.value(g.name.toInt()), count(g.ids.size()),
                         tr("In-progress titles with known totals"), double(g.ids.size()), g.ids});
  }
  charts_[Progress]->setRows(progressRows);
}

void StatsWidget::renderActivity() {
  const auto& activity = activitySummary_;
  activityNote_->setText(
      activity.changes.isEmpty()
          ? tr("No local activity for this selection yet. Progress and score edits you make in "
               "Taiga will appear here. Earlier activity and changes made only on MAL are not "
               "reconstructed.")
          : tr("%1 recorded changes since %2, adding %3 %4 and recording %5 score changes. This is "
               "activity captured in Taiga, not a complete MAL history.")
                .arg(count(activity.changes.size()),
                     QLocale().toString(activity.firstDate, QLocale::ShortFormat),
                     count(activity.addedUnits),
                     kind_->currentIndex() ? tr("chapters") : tr("episodes"),
                     count(activity.scoreChanges)));
  QMap<QString, stats::Group> days, months;
  for (const auto& group : activity.days) days.insert(group.name, group);
  for (const auto& group : activity.months) months.insert(group.name, group);
  QList<ChartRow> dailyRows, monthlyRows;
  const auto today = QDate::currentDate();
  if (!activity.changes.isEmpty()) {
    for (int i = 29; i >= 0; --i) {
      const auto date = today.addDays(-i);
      const auto group = days.value(date.toString(Qt::ISODate));
      dailyRows.append({QString::number(date.day()), count(group.units),
                        QLocale().toString(date, QLocale::LongFormat), double(group.units),
                        group.ids});
    }
    for (int i = 11; i >= 0; --i) {
      const auto date = QDate(today.year(), today.month(), 1).addMonths(-i);
      const auto group = months.value(date.toString("yyyy-MM"));
      monthlyRows.append({date.toString("MMM"), count(group.units), date.toString("MMMM yyyy"),
                          double(group.units), group.ids});
    }
  }
  charts_[DailyActivity]->setRows(dailyRows);
  charts_[MonthlyActivity]->setRows(monthlyRows);
  activityModel_->setRows(activity, summary_.records);
}

void StatsWidget::showTitles(const QList<int>& ids, const QString& label, bool all) {
  drilled_ = !all;
  drillIds_ = all ? QList<int>{} : ids;
  drillLabel_ = all ? QString{} : label;
  backToChart_->setEnabled(drilled_);
  const QSet<int> selected(ids.begin(), ids.end());
  QList<stats::Record> rows;
  for (const auto& r : summary_.records)
    if (all || selected.contains(r.id)) rows.append(r);
  titlesModel_->setRows(std::move(rows));
  updateTitleCount();
}

void StatsWidget::updateTitleCount() {
  const auto visible = titlesProxy_->rowCount(), total = titlesModel_->rowCount();
  const auto amount = visible == total ? tr("%1 titles").arg(count(total))
                                       : tr("%1 of %2 titles").arg(count(visible), count(total));
  selection_->setText((drillLabel_.isEmpty() ? QString{} : drillLabel_ + '\n') + amount +
                      tr(". Double-click or press Enter to open a title."));
  tabs_->setTabText(kTitlesTab, tr("Titles (%1)").arg(count(visible)));
  titlesEmpty_->setText(
      !search_->text().isEmpty()
          ? tr("No titles match this search. Clear the search to see this selection.")
          : tr("No titles in this selection. Choose All filtered titles or clear the global "
               "filters."));
  titlesEmpty_->setVisible(visible == 0);
}
void StatsWidget::drillDown(const QList<int>& ids, const QString& label) {
  if (tabs_->currentIndex() != kTitlesTab) previousTab_ = tabs_->currentIndex();
  search_->clear();
  showTitles(ids, label, false);
  tabs_->setCurrentIndex(kTitlesTab);
  titlesView_->setFocus();
}
}  // namespace gui
