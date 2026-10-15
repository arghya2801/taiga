#include "manga_widget.hpp"

#include <QAction>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>

#include "base/string.hpp"
#include "gui/main/main_window.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/main/status_bar_controller.hpp"
#include "gui/manga/manga_dialog.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/painter_state_saver.hpp"
#include "gui/utils/painters.hpp"
#include "gui/utils/theme.hpp"
#include "gui/utils/widgets.hpp"
#include "media/manga_library.hpp"
#include "sync/myanimelist/myanimelist.hpp"
#include "taiga/accounts.hpp"
#include "taiga/path.hpp"

namespace gui {

namespace {

enum Column {
  kTitle,
  kChapters,
  kVolumes,
  kScore,
  kType,
  kMean,
  kPublishing,
  kRereads,
  kStarted,
  kCompleted,
  kUpdated,
  kColumnCount
};

constexpr int kSortRole = Qt::UserRole;
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kTotalRole = Qt::UserRole + 2;

bool sameListFields(const manga::Entry& first, const manga::Entry& second) {
  return first.status == second.status && first.chaptersRead == second.chaptersRead &&
         first.volumesRead == second.volumesRead && first.score == second.score &&
         first.rereading == second.rereading && first.timesReread == second.timesReread &&
         first.rereadValue == second.rereadValue && first.priority == second.priority &&
         first.tags == second.tags && first.comments == second.comments &&
         first.startedReading == second.startedReading &&
         first.finishedReading == second.finishedReading;
}

// MyAnimeList sends types like "light_novel".
QString formatType(QString type) {
  type.replace('_', ' ');
  if (!type.isEmpty()) type[0] = type[0].toUpper();
  return type;
}

QList<QStandardItem*> makeRow(const manga::Entry& entry) {
  const auto item = [](const QString& text, const QVariant& sortKey, bool centered = true) {
    auto* item = new QStandardItem(text);
    item->setData(sortKey, kSortRole);
    item->setEditable(false);
    if (centered) item->setTextAlignment(Qt::AlignCenter);
    return item;
  };
  QList<QStandardItem*> row{
      item(entry.title, entry.title, false),
      item({}, entry.chaptersRead),
      item(u"%1/%2"_s.arg(entry.volumesRead).arg(formatNumber(entry.volumes, "?")),
           entry.volumesRead),
      item(formatNumber(entry.score), entry.score),
      item(formatType(entry.type), entry.type),
      item(entry.mean > 0 ? QString::number(entry.mean, 'f', 2) : u"-"_s, entry.mean),
      item(formatType(entry.publicationStatus), entry.publicationStatus),
      item(formatNumber(entry.timesReread), entry.timesReread),
      item(entry.startedReading, entry.startedReading),
      item(entry.finishedReading, entry.finishedReading),
      item(QDateTime::fromString(entry.updatedAt, Qt::ISODate)
               .toLocalTime()
               .toString(u"yyyy-MM-dd"_s),
           entry.updatedAt),
  };
  row[kTitle]->setData(entry.id, kIdRole);
  row[kChapters]->setData(entry.chapters, kTotalRole);
  return row;
}

void showStatus(const QString& text, bool spin) {
  mainWindow()->statusBarController()->showMessage({
      .source = StatusBarController::Source::Sync,
      .text = text,
      .spin = spin,
  });
}

// Mirrors `ListItemDelegate`, with the progress bar driven by chapters.
class MangaItemDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    if (index.column() > 0) {
      const PainterStateSaver painterStateSaver(painter);
      painter->setPen(theme.isDark() ? QColor{255, 255, 255, 6} : QColor{0, 0, 0, 6});
      painter->drawLine(option.rect.topLeft(), option.rect.bottomLeft());
    }

    QStyledItemDelegate::paint(painter, option, index);

    if (index.column() == kChapters) {
      const PainterStateSaver painterStateSaver(painter);
      QStyleOptionViewItem opt = option;
      opt.rect.adjust(2, 2, -2, -2);
      paintProgressBar(painter, opt, index.data(kSortRole).toInt(), index.data(kTotalRole).toInt());
    }
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    return index.isValid() ? QSize(0, 24) : QStyledItemDelegate::sizeHint(option, index);
  }
};

}  // namespace

// Set up like the anime `ListView`.
class MangaListView final : public QTreeView {
public:
  explicit MangaListView(QWidget* parent) : QTreeView(parent) {
    setObjectName("animeList");
    setFrameShape(QFrame::Shape::NoFrame);
    setAlternatingRowColors(true);
    setItemDelegate(new MangaItemDelegate(this));
    setAllColumnsShowFocus(true);
    setExpandsOnDoubleClick(false);
    setItemsExpandable(false);
    setRootIsDecorated(false);
    setUniformRowHeights(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
  }

  QString emptyText;

protected:
  void paintEvent(QPaintEvent* event) override {
    if (model() && model()->rowCount() == 0) paintEmptyListText(this, emptyText);
    QTreeView::paintEvent(event);
  }
};

MangaWidget::MangaWidget(QWidget* parent)
    : PageWidget(parent),
      model_(new QStandardItemModel(0, kColumnCount, this)),
      proxyModel_(new QSortFilterProxyModel(this)),
      view_(new MangaListView(this)) {
  setObjectName("mangaWidget");
  model_->setHorizontalHeaderLabels({tr("Title"), tr("Chapters"), tr("Volumes"), tr("Score"),
                                     tr("Type"), tr("Average"), tr("Publishing"), tr("Rereads"),
                                     tr("Started"), tr("Completed"), tr("Last updated")});
  for (int column = kChapters; column < kColumnCount; ++column)
    model_->horizontalHeaderItem(column)->setTextAlignment(Qt::AlignCenter);

  proxyModel_->setSourceModel(model_);
  proxyModel_->setSortRole(kSortRole);
  proxyModel_->setSortCaseSensitivity(Qt::CaseInsensitive);
  proxyModel_->setFilterKeyColumn(kTitle);
  proxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);

  view_->setModel(proxyModel_);
  auto* header = view_->header();
  header->setFirstSectionMovable(true);
  header->setStretchLastSection(false);
  header->setTextElideMode(Qt::ElideRight);
  header->resizeSection(kTitle, 295);
  header->resizeSection(kChapters, 150);
  for (int column = kVolumes; column < kColumnCount; ++column) header->resizeSection(column, 75);
  header->resizeSection(kPublishing, 110);
  header->resizeSection(kUpdated, 100);
  for (int column = kPublishing; column < kColumnCount; ++column) header->hideSection(column);
  setupHeaderMenu(header, u"mangaList"_s);
  view_->sortByColumn(kTitle, Qt::AscendingOrder);
  view_->setSortingEnabled(true);
  layout()->addWidget(view_);

  refreshAction_ =
      m_toolbar->addAction(theme.getIcon("sync"), tr("Refresh"), this, [this] { refresh(); });
  chapterAction_ = m_toolbar->addAction(theme.getIcon("skip_next"), tr("Read next chapter"), this,
                                        [this] { addChapter(); });

  auto* searchBox = mainWindow()->searchBox();
  connect(searchBox, &QLineEdit::textChanged, this, [this](const QString& text) {
    if (!isVisible()) return;
    if (showingResults_ && text.isEmpty()) {
      showingResults_ = false;
      populate();
    } else if (!showingResults_) {
      proxyModel_->setFilterFixedString(text);
    }
  });
  connect(searchBox, &QLineEdit::returnPressed, this, [this] {
    if (isVisible()) search();
  });
  connect(mainWindow()->navigation(), &NavigationWidget::currentMangaStatusChanged, this,
          [this](const QString& status) {
            currentStatus_ = status;
            showingResults_ = false;
            populate();
          });
  connect(view_, &QAbstractItemView::activated, this, [this] { editSelected(); });
  connect(view_, &QWidget::customContextMenuRequested, this, &MangaWidget::showContextMenu);

  auto* service = sync::myanimelist::Service::instance();
  connect(&manga::library, &manga::Library::reset, this, [this] {
    populate();
    updateCounts();
    setBusy(false);
  });
  connect(&manga::library, &manga::Library::changed, this, [this](int id) {
    if (results_.contains(id)) {
      if (manga::library.entries().contains(id))
        results_[id] = manga::library.entries().value(id);
      else {
        results_[id].onList = false;
        results_[id].status.clear();
      }
    }
    updateRow(id);
    updateCounts();
  });
  connect(&manga::library, &manga::Library::errorOccurred, this,
          [this](const QString& error) { setBusy(false, error); });
  connect(&manga::library, &manga::Library::pendingChanged, this, [this] {
    const int count = manga::library.pendingCount();
    showStatus(count
                   ? tr("%n manga change(s) waiting to sync. Failed requests retry automatically.",
                        nullptr, count)
                   : tr("Manga changes saved to MyAnimeList."),
               false);
  });
  connect(service, &sync::myanimelist::Service::mangaListFetched, this, [this] { setBusy(false); });
  connect(service, &sync::myanimelist::Service::mangaSearchCompleted, this,
          [this](const QString& query, const QList<manga::Entry>& entries) {
            setBusy(false);
            if (query != mainWindow()->searchBox()->text().trimmed()) {
              mainWindow()->statusBarController()->clearMessage(StatusBarController::Source::Sync);
              return;
            }
            results_.clear();
            for (const auto& entry : entries) results_.insert(entry.id, entry);
            showingResults_ = true;
            populate();
            showStatus(tr("Found %1 manga on MyAnimeList.").arg(entries.size()), false);
          });
  // MainWindow already puts service errors in the status bar.
  connect(service, &sync::Service::errorOccurred, this, [this] { setBusy(false); });
  connect(service, &sync::Service::authenticationCompleted, this, [this](bool authenticated) {
    if (!authenticated) setBusy(false);
  });

  manga::library.init();
  populate();
  m_toolbar->addAction(theme.getIcon("export_notes"), tr("Export..."), this, [this] {
    QString selected;
    const auto path =
        QFileDialog::getSaveFileName(this, tr("Export manga list"), {},
                                     tr("MyAnimeList XML (*.xml);;Markdown (*.md)"), &selected);
    if (path.isEmpty()) return;
    auto entries = manga::library.entries().values();
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
      return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    setBusy(false, manga::exportList(entries, path, selected.contains("XML"))
                       ? tr("Exported manga list to %1.").arg(path)
                       : tr("Could not export manga list."));
  });
  refresh();
}

void MangaWidget::setBusy(bool busy, const QString& message) {
  busy_ = busy;
  refreshAction_->setEnabled(!busy);
  chapterAction_->setEnabled(!busy && !taiga::accounts.myanimelistAccessToken().empty());
  if (!message.isEmpty()) showStatus(message, busy);
}

void MangaWidget::refresh() {
  if (busy_ || taiga::accounts.myanimelistUsername().empty()) return;
  setBusy(true, tr("Loading manga from MyAnimeList..."));
  manga::library.refresh();
}

void MangaWidget::search() {
  const auto query = mainWindow()->searchBox()->text().trimmed();
  if (busy_ || query.isEmpty()) return;
  setBusy(true, tr("Searching MyAnimeList for \"%1\"...").arg(query));
  sync::myanimelist::Service::instance()->searchManga(query);
}

void MangaWidget::populate() {
  const int previousId = selectedId();

  // Sorting once at the end instead of on every inserted row.
  view_->setUpdatesEnabled(false);
  proxyModel_->setDynamicSortFilter(false);
  rows_.clear();
  model_->removeRows(0, model_->rowCount());
  for (const auto& entry : showingResults_ ? results_ : manga::library.entries()) {
    if (!showingResults_ && !currentStatus_.isEmpty() && entry.status != currentStatus_) continue;
    model_->appendRow(makeRow(entry));
    rows_.insert(entry.id, model_->index(model_->rowCount() - 1, kTitle));
  }
  proxyModel_->setDynamicSortFilter(true);
  view_->setUpdatesEnabled(true);

  proxyModel_->setFilterFixedString(showingResults_ ? QString{}
                                                    : mainWindow()->searchBox()->text());
  view_->emptyText = taiga::accounts.myanimelistUsername().empty()
                         ? tr("Connect a MyAnimeList account in Settings to see your manga.")
                         : tr("No manga found.");
  selectId(previousId);
}

void MangaWidget::updateRow(int id) {
  const auto& source = showingResults_ ? results_ : manga::library.entries();
  const bool visible = source.contains(id) && (showingResults_ || currentStatus_.isEmpty() ||
                                               source.value(id).status == currentStatus_);
  const auto index = rows_.value(id);
  if (!visible) {
    rows_.remove(id);
    if (index.isValid()) model_->removeRow(index.row());
    return;
  }
  const auto row = makeRow(source.value(id));
  if (!index.isValid()) {
    model_->appendRow(row);
    rows_.insert(id, model_->index(model_->rowCount() - 1, kTitle));
    return;
  }
  proxyModel_->setDynamicSortFilter(false);
  for (int column = 0; column < row.size(); ++column) {
    auto* target = model_->item(index.row(), column);
    for (const auto role :
         {int(Qt::DisplayRole), kSortRole, kIdRole, kTotalRole, int(Qt::TextAlignmentRole)})
      target->setData(row[column]->data(role), role);
    delete row[column];
  }
  proxyModel_->setDynamicSortFilter(true);
}

void MangaWidget::updateCounts() {
  QHash<QString, int> counts;
  for (const auto& entry : manga::library.entries()) ++counts[entry.status];
  mainWindow()->navigation()->updateMangaCounts(counts);
}

void MangaWidget::selectId(int id) {
  if (!id) return;
  const auto matches =
      proxyModel_->match(proxyModel_->index(0, kTitle), kIdRole, id, 1, Qt::MatchExactly);
  if (!matches.isEmpty()) view_->setCurrentIndex(matches.first());
}

int MangaWidget::selectedId() const {
  const auto index = view_->currentIndex();
  return index.isValid() ? index.siblingAtColumn(kTitle).data(kIdRole).toInt() : 0;
}

const manga::Entry* MangaWidget::selectedEntry() const {
  const int id = selectedId();
  if (const auto it = manga::library.entries().constFind(id); it != manga::library.entries().cend())
    return &*it;
  if (const auto it = results_.constFind(id); it != results_.cend()) return &*it;
  return nullptr;
}

void MangaWidget::addChapter() {
  if (busy_ || taiga::accounts.myanimelistAccessToken().empty()) return;
  const int id = selectedId();
  if (!manga::library.entries().contains(id)) return;
  auto entry = manga::library.entries().value(id);
  if (entry.chapters > 0 && entry.chaptersRead >= entry.chapters) return;
  ++entry.chaptersRead;
  if (entry.status == "plan_to_read") entry.status = "reading";
  if (manga::library.save(entry)) showStatus(tr("Chapter progress saved locally."), false);
}

void MangaWidget::editSelected() {
  if (busy_) return;
  if (const auto* entry = selectedEntry()) editEntry(*entry);
}

void MangaWidget::editEntry(manga::Entry entry) {
  MangaDialog dialog(this, entry);
  if (dialog.exec() != QDialog::Accepted) return;

  if (taiga::accounts.myanimelistAccessToken().empty()) {
    QMessageBox::information(this, tr("Manga list"),
                             tr("Log in to MyAnimeList in Settings to edit manga."));
    return;
  }
  if (dialog.removeRequested()) {
    manga::library.remove(entry.id);
    return;
  }
  const auto edited = dialog.editedEntry();
  if (entry.onList && sameListFields(entry, edited)) return;
  manga::library.save(edited);
}

void MangaWidget::showContextMenu(const QPoint& pos) {
  const auto* entry = selectedEntry();
  if (!entry || !view_->indexAt(pos).isValid()) return;

  QMenu menu(this);
  menu.addAction(theme.getIcon("edit"), entry->onList ? tr("Edit...") : tr("Add to list..."), this,
                 [this] { editSelected(); });
  if (manga::library.entries().contains(entry->id)) {
    menu.addAction(chapterAction_);
  }
  menu.addSeparator();
  menu.addAction(theme.getIcon("open_in_new"), tr("Open on MyAnimeList"), this, [id = entry->id] {
    QDesktopServices::openUrl(QUrl{u"https://myanimelist.net/manga/%1"_s.arg(id)});
  });
  menu.exec(view_->viewport()->mapToGlobal(pos));
}

}  // namespace gui
