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

#include "torrents_widget.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QHeaderView>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>

#include "base/string.hpp"
#include "gui/main/main_window.hpp"
#include "gui/main/status_bar_controller.hpp"
#include "gui/settings/settings_dialog.hpp"
#include "gui/utils/painters.hpp"
#include "gui/utils/theme.hpp"
#include "gui/utils/widgets.hpp"
#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"
#include "track/feed.hpp"

namespace gui {

namespace {

enum Column { kAnime, kEpisode, kGroup, kResolution, kSize, kDate, kFile, kColumnCount };

constexpr int kIndexRole = Qt::UserRole;
constexpr int kSortRole = Qt::UserRole + 1;

// Sorts numbers and dates by value instead of by their text.
class Row final : public QTreeWidgetItem {
public:
  using QTreeWidgetItem::QTreeWidgetItem;

  bool operator<(const QTreeWidgetItem& other) const override {
    const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
    const auto a = data(column, kSortRole);
    const auto b = other.data(column, kSortRole);
    if (a.isValid() && b.isValid()) return QVariant::compare(a, b) < 0;
    return text(column).compare(other.text(column), Qt::CaseInsensitive) < 0;
  }
};

class TorrentList final : public QTreeWidget {
public:
  using QTreeWidget::QTreeWidget;
  QString emptyText;

protected:
  void paintEvent(QPaintEvent* event) override {
    if (topLevelItemCount() == 0) paintEmptyListText(this, emptyText);
    QTreeWidget::paintEvent(event);
  }
};

void showStatus(const QString& text, bool spin = false) {
  mainWindow()->statusBarController()->showMessage({
      .source = StatusBarController::Source::Library,
      .text = text,
      .spin = spin,
  });
}

}  // namespace

TorrentsWidget::TorrentsWidget(QWidget* parent) : PageWidget(parent) {
  auto* aggregator = track::feed::aggregator();

  m_checkAction = m_toolbar->addAction(theme.getIcon("sync"), tr("Check new torrents"), this,
                                       [aggregator] { aggregator->check(); });
  m_toolbar->addAction(theme.getIcon("cloud_download"), tr("Download checked torrents"), this,
                       &TorrentsWidget::downloadChecked);
  m_toolbar->addAction(theme.getIcon("settings"), tr("Torrent settings and filters"), this,
                       [this] { SettingsDialog::show(mainWindow()); });

  auto* view = new TorrentList(this);
  m_view = view;
  view->setObjectName("animeList");
  view->setFrameShape(QFrame::NoFrame);
  view->setAlternatingRowColors(true);
  view->setRootIsDecorated(false);
  view->setUniformRowHeights(true);
  view->setAllColumnsShowFocus(true);
  view->setSelectionMode(QAbstractItemView::ExtendedSelection);
  view->setContextMenuPolicy(Qt::CustomContextMenu);
  view->setSortingEnabled(true);
  view->setHeaderLabels(
      {tr("Anime"), tr("Episode"), tr("Group"), tr("Resolution"), tr("Size"), tr("Date"),
       tr("File name")});
  view->emptyText = tr("Check new torrents, or search for an anime with the search box.");

  auto* header = view->header();
  header->setStretchLastSection(false);
  header->setFirstSectionMovable(true);
  header->resizeSection(kAnime, 260);
  header->resizeSection(kEpisode, 70);
  header->resizeSection(kGroup, 120);
  header->resizeSection(kResolution, 80);
  header->resizeSection(kSize, 80);
  header->resizeSection(kDate, 130);
  header->resizeSection(kFile, 400);
  setupHeaderMenu(header, u"torrentList"_s);
  view->sortByColumn(kDate, Qt::DescendingOrder);
  layout()->addWidget(view);

  connect(view, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* row) {
    if (const int index = itemIndex(row); index >= 0) {
      track::feed::aggregator()->download(track::feed::aggregator()->items().at(index));
    }
  });
  connect(view, &QWidget::customContextMenuRequested, this, &TorrentsWidget::showContextMenu);

  connect(aggregator, &track::feed::Aggregator::itemsChanged, this, &TorrentsWidget::populate);
  connect(aggregator, &track::feed::Aggregator::errorOccurred, this, [](const QString& message) {
    showStatus(tr("Couldn't get torrents: %1").arg(message));
  });

  auto* searchBox = mainWindow()->searchBox();
  connect(searchBox, &QLineEdit::returnPressed, this, [this, searchBox] {
    if (isVisible() && !searchBox->text().trimmed().isEmpty()) search(searchBox->text().trimmed());
  });

  populate();
}

void TorrentsWidget::search(const QString& title) {
  track::feed::aggregator()->search(title);
}

int TorrentsWidget::itemIndex(const QTreeWidgetItem* row) const {
  return row ? row->data(kAnime, kIndexRole).toInt() : -1;
}

void TorrentsWidget::populate() {
  using track::feed::ItemState;

  const auto* aggregator = track::feed::aggregator();
  m_checkAction->setEnabled(!aggregator->isBusy());
  if (aggregator->isBusy()) {
    showStatus(tr("Getting torrents..."), true);
  } else {
    mainWindow()->statusBarController()->clearMessage(StatusBarController::Source::Library);
  }

  const QLocale locale;
  const auto dimmed = palette().color(QPalette::Disabled, QPalette::Text);

  m_view->setUpdatesEnabled(false);
  m_view->setSortingEnabled(false);
  m_view->clear();

  const auto& items = aggregator->items();
  int selected = 0;
  for (int i = 0; i < items.size(); ++i) {
    const auto& item = items.at(i);
    if (item.state == ItemState::Hidden) continue;

    const auto anime = anime::db.item(item.animeId);
    auto* row = new Row(m_view);
    row->setText(kAnime, anime ? QString::fromStdString(anime::preferredTitle(*anime))
                               : item.animeTitle);
    row->setText(kEpisode, item.episode ? QString::number(item.episode) : QString{});
    row->setData(kEpisode, kSortRole, item.episode);
    row->setText(kGroup, item.group);
    row->setText(kResolution, item.resolution);
    row->setText(kSize, item.size ? locale.formattedDataSize(item.size) : QString{});
    row->setData(kSize, kSortRole, item.size);
    row->setData(kDate, kSortRole, item.date);
    row->setText(kDate, item.date.isValid() ? locale.toString(item.date.toLocalTime(),
                                                              QLocale::ShortFormat)
                                            : QString{});
    row->setText(kFile, item.title);
    row->setToolTip(kFile, item.description.isEmpty() ? item.title : item.description);
    row->setData(kAnime, kIndexRole, i);
    row->setCheckState(kAnime, item.state == ItemState::Selected ? Qt::Checked : Qt::Unchecked);
    for (int column = kEpisode; column < kColumnCount; ++column) {
      if (column != kFile) row->setTextAlignment(column, Qt::AlignCenter);
    }
    if (item.isDiscarded()) {
      for (int column = 0; column < kColumnCount; ++column) row->setForeground(column, dimmed);
    }
    if (item.state == ItemState::Selected) ++selected;
  }

  m_view->setSortingEnabled(true);
  m_view->setUpdatesEnabled(true);

  if (!aggregator->isBusy() && !items.isEmpty()) {
    showStatus(tr("%n torrent(s), %1 selected by filters.", nullptr, items.size()).arg(selected));
  }
}

void TorrentsWidget::downloadChecked() {
  auto* aggregator = track::feed::aggregator();
  const auto items = aggregator->items();  // downloading archives, which changes the list
  QList<int> indexes;
  for (int i = 0; i < m_view->topLevelItemCount(); ++i) {
    const auto* row = m_view->topLevelItem(i);
    if (row->checkState(kAnime) == Qt::Checked) indexes.append(itemIndex(row));
  }
  if (indexes.isEmpty()) {
    showStatus(tr("Check the torrents you want to download first."));
    return;
  }
  for (const int index : indexes) aggregator->download(items.at(index));
}

void TorrentsWidget::showContextMenu(const QPoint& pos) {
  const int index = itemIndex(m_view->itemAt(pos));
  if (index < 0) return;

  auto* aggregator = track::feed::aggregator();
  const auto item = aggregator->items().at(index);

  QMenu menu(this);
  menu.addAction(theme.getIcon("cloud_download"), tr("Download"), this,
                 [aggregator, item] { aggregator->download(item); });
  menu.addAction(tr("Discard"), this, [aggregator, item] { aggregator->archive(item); });
  menu.addSeparator();
  if (item.guid.startsWith(u"http")) {
    menu.addAction(theme.getIcon("open_in_new"), tr("Open info page"), this,
                   [item] { QDesktopServices::openUrl(QUrl{item.guid}); });
  }
  if (!item.magnet.isEmpty()) {
    menu.addAction(theme.getIcon("content_copy"), tr("Copy magnet link"), this,
                   [item] { QApplication::clipboard()->setText(item.magnet); });
  }

  // Quick filters, like v1's.
  if (item.animeId) {
    const auto anime = anime::db.item(item.animeId);
    const auto title = anime ? QString::fromStdString(anime::preferredTitle(*anime))
                             : item.animeTitle;
    menu.addSeparator();
    if (!item.group.isEmpty()) {
      menu.addAction(tr("Prefer %1 for %2").arg(item.group, title), this, [aggregator, item,
                                                                            title] {
        using namespace track::feed;
        auto filters = aggregator->filters();
        Filter filter{.name = tr("[%1] %2").arg(item.group, title),
                      .action = FilterAction::Prefer,
                      .animeIds = {item.animeId},
                      .conditions = {{FilterElement::Group, FilterOperator::Equals, item.group}}};
        if (!item.resolution.isEmpty()) {
          filter.conditions.append(
              {FilterElement::Resolution, FilterOperator::Equals, item.resolution});
        }
        filters.prepend(filter);
        aggregator->setFilters(filters);
      });
    }
    menu.addAction(tr("Discard all torrents for %1").arg(title), this, [aggregator, item, title] {
      using namespace track::feed;
      auto filters = aggregator->filters();
      filters.prepend({.name = tr("Discard %1").arg(title),
                       .action = FilterAction::Discard,
                       .option = FilterOption::Hide,
                       .animeIds = {item.animeId},
                       .conditions = {{FilterElement::AnimeId, FilterOperator::Equals,
                                       QString::number(item.animeId)}}});
      aggregator->setFilters(filters);
    });
  }

  menu.exec(m_view->viewport()->mapToGlobal(pos));
}

}  // namespace gui
