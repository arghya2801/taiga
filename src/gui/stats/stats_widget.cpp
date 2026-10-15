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
#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"
#include "media/manga_library.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
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
  Progress
};
enum Role { ValueRole = Qt::UserRole + 1, RatioRole, NoteRole, IdsRole, ValueTextRole };
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
        activate(index.data(IdsRole).value<QList<int>>(), index.data(Qt::UserRole).toString());
    };
    connect(this, &QListView::clicked, this, open);
    connect(this, &QListView::activated, this, open);
  }
  std::function<void(const QList<int>&, const QString&)> activate;
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
      p.drawText(
          viewport()->rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
          tr("No titles match this chart. Try clearing the filters or synchronizing your list."));
    }
  }

private:
  void fitColumns() {
    if (columns_)
      setGridSize(QSize(std::max(38, viewport()->width() / std::max(1, model_->rowCount())), 180));
  }
  bool columns_;
  int visibleRows_;
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
  auto* filterToggle = new QPushButton(tr("Filters"), this);
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
  for (auto* box : {kind_, status_, format_, year_, genre_, studio_})
    connect(box, &QComboBox::currentIndexChanged, this, [this] { invalidate(); });

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
  charts_.resize(16);
  const auto chart = [&](StatsCard* card, int index, bool columns = false, int rows = 6) {
    auto* view = new StatsChart(columns, rows, card);
    view->setObjectName(QStringLiteral("statsChart%1").arg(index));
    view->setAccessibleName(card->findChild<QLabel*>()->text());
    view->activate = [this, index](const QList<int>& ids, const QString& label) {
      drillSource_ = charts_[index];
      drillDown(ids, charts_[index]->accessibleName() + ": " + label);
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
      if (r.status != "Completed" && r.status != "Dropped" && r.updated.isValid() &&
          r.updated.daysTo(QDate::currentDate()) >= 180)
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
  timelineGrid->add(
      chart(new StatsCard(tr("How close are you to finishing?"),
                          tr("Watching, reading and on-hold titles with known totals.")),
            Progress));
  timelineBox->addStretch();

  auto* library = new QWidget(tabs_);
  auto* libraryBox = new QVBoxLayout(library);
  libraryBox->setContentsMargins(20, 16, 20, 16);
  libraryBox->setSpacing(12);
  selection_ = new QLabel(library);
  selection_->setWordWrap(true);
  libraryBox->addWidget(selection_);
  auto* titleBar = new QHBoxLayout;
  auto* back = new QPushButton(tr("Back to chart"), library);
  back->setObjectName("statsBackToChart");
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
  connect(all, &QPushButton::clicked, this, [this] {
    search_->clear();
    showTitles();
  });
  const auto returnToChart = [this] {
    if (tabs_->currentIndex() != 4) return;
    tabs_->setCurrentIndex(previousTab_);
    if (drillSource_) drillSource_->setFocus();
  };
  connect(back, &QPushButton::clicked, this, returnToChart);
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
  manga::library.init();
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
    box->setCurrentIndex(std::max(0, box->findData(old)));
  }
}

void StatsWidget::invalidate() {
  dirty_ = true;
  ++revision_;
  titlesView_->setEnabled(false);
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
  QtConcurrent::run([records = std::move(records), filter] {
    return stats::calculate(records, filter);
  }).then(this, [this, revision](stats::Summary result) {
    calculating_ = false;
    if (revision != revision_) {
      debounce_.start();
      return;
    }
    dirty_ = false;
    summary_ = std::move(result);
    render();
    titlesView_->setEnabled(true);
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
                       "Completed and dropped titles are excluded.")
                        .arg(count(s.remainingUnits), unit,
                             manga ? QString{} : tr(" (%1 hours)").arg(hours(s.remainingSeconds)),
                             count(s.stale)));
  repeats_->setText(
      tr("%1 recorded repeats across %2 titles, adding %3 %4 with known totals.")
          .arg(count(s.repeatCount), count(s.repeatedTitles), count(s.repeatUnits), unit));
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
    if (r.status != "Completed" && r.status != "Dropped" && r.total > r.progress)
      unfinished.append(r);
    if (r.repeats > 0) repeated.append(r);
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
                       count(r.repeats),
                       manga ? tr("Recorded rereads") : tr("Recorded rewatches"),
                       double(r.repeats),
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
  const auto previousYear = timelineYear_->currentData().toString();
  timelineYear_->clear();
  const auto years = s.groups.value(stats::Dimension::CompletionYear);
  for (auto it = years.crbegin(); it != years.crend(); ++it)
    if (it->name != "Unknown") timelineYear_->addItem(it->name, it->name);
  if (timelineYear_->count() == 0) timelineYear_->addItem(tr("No dated completions"), QString{});
  const int previousIndex = timelineYear_->findData(previousYear);
  if (previousIndex >= 0) timelineYear_->setCurrentIndex(previousIndex);
  renderTimeline();
  search_->clear();
  showTitles();
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

void StatsWidget::showTitles(const QList<int>& ids, const QString& label, bool all) {
  const QSet<int> selected(ids.begin(), ids.end());
  QList<stats::Record> rows;
  for (const auto& r : summary_.records)
    if (all || selected.contains(r.id)) rows.append(r);
  titlesModel_->setRows(std::move(rows));
  selection_->setText(
      tr("%1%2 titles. Double-click or press Enter to open a title.")
          .arg(label.isEmpty() ? QString{} : label + "\n", count(titlesModel_->rowCount())));
  tabs_->setTabText(4, tr("Titles (%1)").arg(count(titlesModel_->rowCount())));
}
void StatsWidget::drillDown(const QList<int>& ids, const QString& label) {
  if (tabs_->currentIndex() != 4) previousTab_ = tabs_->currentIndex();
  search_->clear();
  showTitles(ids, label, false);
  tabs_->setCurrentIndex(4);
  titlesView_->setFocus();
}
}  // namespace gui
