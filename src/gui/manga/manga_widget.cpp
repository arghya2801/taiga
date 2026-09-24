#include "manga_widget.hpp"

#include <QCryptographicHash>
#include <QAbstractItemView>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <utility>

#include "base/string.hpp"
#include "gui/main/main_window.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/manga/manga_dialog.hpp"
#include "sync/myanimelist/myanimelist.hpp"
#include "taiga/accounts.hpp"
#include "taiga/path.hpp"

namespace gui {

namespace {

constexpr auto kStatuses = std::to_array<std::pair<const char*, const char*>>({
    {"reading", "Reading"},
    {"completed", "Completed"},
    {"on_hold", "On hold"},
    {"dropped", "Dropped"},
    {"plan_to_read", "Plan to read"},
});

QString statusLabel(const QString& status) {
  for (const auto& [value, label] : kStatuses) {
    if (status == QLatin1String(value)) return QLatin1String(label);
  }
  return status;
}

bool sameListFields(const manga::Entry& first, const manga::Entry& second) {
  return first.status == second.status && first.chaptersRead == second.chaptersRead &&
         first.volumesRead == second.volumesRead && first.score == second.score &&
         first.rereading == second.rereading && first.timesReread == second.timesReread &&
         first.rereadValue == second.rereadValue && first.priority == second.priority &&
         first.tags == second.tags && first.comments == second.comments;
}

QTableWidget* makeTable(QWidget* parent) {
  auto* table = new QTableWidget(parent);
  table->setColumnCount(7);
  table->setHorizontalHeaderLabels(
      {"Title", "Chapters", "Volumes", "Status", "Score", "Type", "Mean"});
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setAlternatingRowColors(true);
  table->setSortingEnabled(true);
  table->sortItems(0, Qt::AscendingOrder);
  table->verticalHeader()->hide();
  table->horizontalHeader()->setStretchLastSection(false);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  const int widths[] = {110, 100, 110, 55, 85, 60};
  for (int column = 1; column < 7; ++column)
    table->setColumnWidth(column, widths[column - 1]);
  return table;
}

QTableWidgetItem* numberItem(int value, const QString& text) {
  class NumericItem final : public QTableWidgetItem {
  public:
    explicit NumericItem(const QString& text) : QTableWidgetItem(text) {}
    bool operator<(const QTableWidgetItem& other) const override {
      return data(Qt::UserRole).toInt() < other.data(Qt::UserRole).toInt();
    }
  };
  auto* item = new NumericItem(text);
  item->setData(Qt::UserRole, value);
  return item;
}

QString cachePath() {
  const auto username = QByteArray::fromStdString(taiga::accounts.myanimelistUsername());
  if (username.isEmpty()) return {};
  const auto key = QCryptographicHash::hash(username, QCryptographicHash::Sha256).toHex();
  return u"%1/manga_%2.json"_s.arg(QString::fromStdString(taiga::get_data_path()),
                                    QString::fromLatin1(key.constData()));
}

}  // namespace

MangaWidget::MangaWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  tabs_ = new QTabWidget(this);
  layout->addWidget(tabs_);
  messageLabel_ = new QLabel(this);
  layout->addWidget(messageLabel_);

  auto* listPage = new QWidget(tabs_);
  auto* listLayout = new QVBoxLayout(listPage);
  auto* listBar = new QHBoxLayout();
  filterBox_ = new QLineEdit(listPage);
  filterBox_->setPlaceholderText(tr("Filter manga"));
  filterBox_->setClearButtonEnabled(true);
  refreshButton_ = new QPushButton(tr("Refresh"), listPage);
  chapterButton_ = new QPushButton(tr("+1 chapter"), listPage);
  editButton_ = new QPushButton(tr("Details"), listPage);
  listBar->addWidget(filterBox_, 1);
  listBar->addWidget(refreshButton_);
  listBar->addWidget(chapterButton_);
  listBar->addWidget(editButton_);
  listLayout->addLayout(listBar);
  listTable_ = makeTable(listPage);
  listLayout->addWidget(listTable_);
  tabs_->addTab(listPage, tr("My manga"));

  auto* searchPage = new QWidget(tabs_);
  auto* searchLayout = new QVBoxLayout(searchPage);
  auto* searchBar = new QHBoxLayout();
  searchBox_ = new QLineEdit(searchPage);
  searchBox_->setPlaceholderText(tr("Search MyAnimeList manga"));
  searchBox_->setClearButtonEnabled(true);
  searchButton_ = new QPushButton(tr("Search"), searchPage);
  addButton_ = new QPushButton(tr("Details / add"), searchPage);
  searchBar->addWidget(searchBox_, 1);
  searchBar->addWidget(searchButton_);
  searchBar->addWidget(addButton_);
  searchLayout->addLayout(searchBar);
  searchTable_ = makeTable(searchPage);
  searchLayout->addWidget(searchTable_);
  tabs_->addTab(searchPage, tr("Find manga"));

  auto* service = sync::myanimelist::Service::instance();
  connect(refreshButton_, &QPushButton::clicked, this, [this] { refresh(); });
  connect(searchButton_, &QPushButton::clicked, this, [this] { search(); });
  connect(searchBox_, &QLineEdit::returnPressed, this, [this] { search(); });
  connect(filterBox_, &QLineEdit::textChanged, this, [this] { filterList(); });
  connect(mainWindow()->navigation(), &NavigationWidget::currentMangaStatusChanged, this,
          [this](const QString& status) {
            currentStatus_ = status;
            tabs_->setCurrentIndex(0);
            filterList();
          });
  connect(editButton_, &QPushButton::clicked, this, [this] { editSelected(listTable_); });
  connect(addButton_, &QPushButton::clicked, this, [this] { editSelected(searchTable_); });
  connect(listTable_, &QTableWidget::itemDoubleClicked, this,
          [this] { editSelected(listTable_); });
  connect(searchTable_, &QTableWidget::itemDoubleClicked, this,
          [this] { editSelected(searchTable_); });
  connect(chapterButton_, &QPushButton::clicked, this, [this, service] {
    if (busy_ || taiga::accounts.myanimelistAccessToken().empty()) return;
    const int id = selectedId(listTable_);
    if (!id || !library_.contains(id)) return;
    auto entry = library_.value(id);
    if (entry.chapters > 0 && entry.chaptersRead >= entry.chapters) return;
    const auto previous = entry;
    ++entry.chaptersRead;
    if (entry.status == "plan_to_read") entry.status = "reading";
    setBusy(true, tr("Saving manga progress..."));
    service->updateMangaEntry(entry, previous);
  });

  connect(service, &sync::myanimelist::Service::mangaListFetched, this,
          [this](const QList<manga::Entry>& entries) {
            library_.clear();
            for (const auto& entry : entries) library_.insert(entry.id, entry);
            populate(listTable_, entries);
            filterList();
            updateCounts();
            saveCache();
            setBusy(false, tr("%1 manga loaded.").arg(entries.size()));
          });
  connect(service, &sync::myanimelist::Service::mangaSearchCompleted, this,
          [this](const QString& query, const QList<manga::Entry>& entries) {
            if (query != searchBox_->text().trimmed()) {
              setBusy(false, tr("Search text changed. Press Search for new results."));
              return;
            }
            results_.clear();
            for (const auto& entry : entries) results_.insert(entry.id, entry);
            populate(searchTable_, entries);
            setBusy(false, tr("%1 results.").arg(entries.size()));
          });
  connect(service, &sync::myanimelist::Service::mangaEntryUpdated, this,
          [this](const manga::Entry& entry) {
            library_.insert(entry.id, entry);
            if (results_.contains(entry.id)) results_.insert(entry.id, entry);
            populate(listTable_, library_.values());
            populate(searchTable_, results_.values());
            filterList();
            updateCounts();
            saveCache();
            setBusy(false, tr("Manga saved to MyAnimeList."));
          });
  connect(service, &sync::myanimelist::Service::mangaEntryDeleted, this, [this](int id) {
    library_.remove(id);
    if (results_.contains(id)) {
      auto entry = results_.value(id);
      entry.onList = false;
      entry.status.clear();
      entry.chaptersRead = entry.volumesRead = entry.score = 0;
      results_.insert(id, entry);
    }
    populate(listTable_, library_.values());
    populate(searchTable_, results_.values());
    filterList();
    updateCounts();
    saveCache();
    setBusy(false, tr("Manga removed from MyAnimeList."));
  });
  connect(service, &sync::Service::errorOccurred, this, [this](const QString& message) {
    if (!busy_) return;
    setBusy(false, message);
  });
  connect(service, &sync::Service::authenticationCompleted, this, [this](bool authenticated) {
    if (!authenticated && busy_) setBusy(false, tr("MyAnimeList login failed."));
  });

  loadCache();
  setBusy(false);
  refresh();
}

void MangaWidget::loadCache() {
  QFile file(cachePath());
  if (!file.open(QIODevice::ReadOnly)) return;
  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isArray()) return;
  for (const auto& value : document.array()) {
    const auto object = value.toObject();
    manga::Entry entry;
    entry.id = object["id"].toInt();
    entry.title = object["title"].toString();
    entry.type = object["type"].toString();
    entry.coverUrl = object["coverUrl"].toString();
    entry.status = object["status"].toString();
    entry.chapters = object["chapters"].toInt();
    entry.volumes = object["volumes"].toInt();
    entry.chaptersRead = object["chaptersRead"].toInt();
    entry.volumesRead = object["volumesRead"].toInt();
    entry.score = object["score"].toInt();
    entry.rereading = object["rereading"].toBool();
    entry.timesReread = object["timesReread"].toInt();
    entry.rereadValue = object["rereadValue"].toInt();
    entry.priority = object["priority"].toInt();
    entry.comments = object["comments"].toString();
    for (const auto& tag : object["tags"].toArray()) entry.tags.append(tag.toString());
    entry.mean = object["mean"].toDouble();
    entry.onList = true;
    if (entry.id > 0 && !entry.title.isEmpty()) library_.insert(entry.id, entry);
  }
  populate(listTable_, library_.values());
  filterList();
  updateCounts();
  messageLabel_->setText(tr("Showing saved manga while MyAnimeList refreshes."));
}

void MangaWidget::saveCache() const {
  const auto path = cachePath();
  if (path.isEmpty()) return;
  QDir().mkpath(QFileInfo(path).absolutePath());
  QJsonArray entries;
  for (const auto& entry : library_) {
    entries.append(QJsonObject{{"id", entry.id},
                               {"title", entry.title},
                               {"type", entry.type},
                               {"coverUrl", entry.coverUrl},
                               {"status", entry.status},
                               {"chapters", entry.chapters},
                               {"volumes", entry.volumes},
                               {"chaptersRead", entry.chaptersRead},
                               {"volumesRead", entry.volumesRead},
                               {"score", entry.score},
                               {"rereading", entry.rereading},
                               {"timesReread", entry.timesReread},
                               {"rereadValue", entry.rereadValue},
                               {"priority", entry.priority},
                               {"tags", QJsonArray::fromStringList(entry.tags)},
                               {"comments", entry.comments},
                               {"mean", entry.mean}});
  }
  QSaveFile file(path);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(entries).toJson(QJsonDocument::Compact));
    file.commit();
  }
}

void MangaWidget::setBusy(bool busy, const QString& message) {
  busy_ = busy;
  refreshButton_->setEnabled(!busy);
  searchButton_->setEnabled(!busy);
  editButton_->setEnabled(!busy);
  addButton_->setEnabled(!busy);
  chapterButton_->setEnabled(!busy && !taiga::accounts.myanimelistAccessToken().empty());
  if (!message.isEmpty()) {
    messageLabel_->setText(message);
  }
}

void MangaWidget::refresh() {
  if (busy_) return;
  if (taiga::accounts.myanimelistUsername().empty()) {
    messageLabel_->setText(tr("Connect a MyAnimeList account in Settings to load manga."));
    return;
  }
  setBusy(true, tr("Loading manga from MyAnimeList..."));
  sync::myanimelist::Service::instance()->fetchMangaList();
}

void MangaWidget::search() {
  if (busy_) return;
  const auto query = searchBox_->text().trimmed();
  if (query.isEmpty()) return;
  setBusy(true, tr("Searching MyAnimeList..."));
  sync::myanimelist::Service::instance()->searchManga(query);
}

void MangaWidget::populate(QTableWidget* table, const QList<manga::Entry>& entries) {
  const int previousId = selectedId(table);
  table->setSortingEnabled(false);
  table->setRowCount(entries.size());
  for (int row = 0; row < entries.size(); ++row) {
    const auto& entry = entries.at(row);
    auto* title = new QTableWidgetItem(entry.title);
    title->setData(Qt::UserRole, entry.id);
    table->setItem(row, 0, title);
    table->setItem(row, 1, numberItem(entry.chaptersRead,
                                      u"%1 / %2"_s.arg(entry.chaptersRead).arg(
                                          entry.chapters ? QString::number(entry.chapters) : u"?"_s)));
    table->setItem(row, 2, numberItem(entry.volumesRead,
                                      u"%1 / %2"_s.arg(entry.volumesRead).arg(
                                          entry.volumes ? QString::number(entry.volumes) : u"?"_s)));
    table->setItem(row, 3, new QTableWidgetItem(statusLabel(entry.status)));
    table->setItem(row, 4, numberItem(entry.score, entry.score ? QString::number(entry.score) : QString{}));
    table->setItem(row, 5, new QTableWidgetItem(entry.type));
    table->setItem(row, 6, numberItem(qRound(entry.mean * 100),
                                      entry.mean > 0 ? QString::number(entry.mean, 'f', 2) : QString{}));
    if (entry.id == previousId) table->selectRow(row);
  }
  table->setSortingEnabled(true);
}

void MangaWidget::filterList() {
  const auto text = filterBox_->text();
  for (int row = 0; row < listTable_->rowCount(); ++row) {
    const int id = listTable_->item(row, 0)->data(Qt::UserRole).toInt();
    const auto entry = library_.value(id);
    listTable_->setRowHidden(row, !entry.title.contains(text, Qt::CaseInsensitive) ||
                                      (!currentStatus_.isEmpty() && entry.status != currentStatus_));
  }
}

void MangaWidget::updateCounts() {
  QHash<QString, int> counts;
  for (const auto& entry : library_) ++counts[entry.status];
  mainWindow()->navigation()->updateMangaCounts(counts);
}

int MangaWidget::selectedId(QTableWidget* table) const {
  const int row = table->currentRow();
  return row >= 0 && table->item(row, 0) ? table->item(row, 0)->data(Qt::UserRole).toInt() : 0;
}

void MangaWidget::editSelected(QTableWidget* table) {
  if (busy_) return;
  const int id = selectedId(table);
  if (!id) return;
  if (library_.contains(id)) {
    editEntry(library_.value(id));
  } else if (results_.contains(id)) {
    editEntry(results_.value(id));
  }
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
    setBusy(true, tr("Removing manga..."));
    sync::myanimelist::Service::instance()->deleteMangaEntry(entry.id);
    return;
  }
  const auto edited = dialog.editedEntry();
  if (entry.onList && sameListFields(entry, edited)) return;
  setBusy(true, tr("Saving manga to MyAnimeList..."));
  sync::myanimelist::Service::instance()->updateMangaEntry(edited, entry);
}

}  // namespace gui
