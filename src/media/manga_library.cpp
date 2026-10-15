#include "manga_library.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QXmlStreamWriter>
#include <QtConcurrentRun>

#include "sync/myanimelist/myanimelist.hpp"
#include "taiga/accounts.hpp"
#include "taiga/path.hpp"
#include "taiga/settings.hpp"

namespace manga {

Library library;

namespace {
bool writeJson(const QString& path, const QJsonDocument& doc) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  const auto bytes = doc.toJson(QJsonDocument::Compact);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QJsonDocument readJson(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()) : QJsonDocument{};
}
}  // namespace

Library::Library() {
  writer_.setMaxThreadCount(1);  // snapshots commit in order, including across account switches
  retry_.setInterval(60000);
  connect(&retry_, &QTimer::timeout, this, &Library::process);
}
Library::~Library() {
  writer_.waitForDone();
}

QString Library::path(const QString& suffix) const {
  const auto key = QCryptographicHash::hash(account_.toUtf8(), QCryptographicHash::Sha256).toHex();
  return QStringLiteral("%1/manga_%2%3.json")
      .arg(QString::fromStdString(taiga::get_data_path()), QString::fromLatin1(key), suffix);
}

void Library::init() {
  if (initialized_) {
    loadAccount();
    return;
  }
  initialized_ = true;
  auto* service = sync::myanimelist::Service::instance();
  connect(service, &sync::Service::authenticationCompleted, this, [this](bool ok) {
    if (ok) {
      loadAccount();
      process();
    } else {
      processing_ = 0;
      refreshing_ = false;
    }
  });
  connect(service, &sync::myanimelist::Service::mangaListFetched, this,
          [this](const QList<Entry>& entries) {
            refreshing_ = false;
            if (!ready_ || refreshVersion_ != version_) return;
            entries_.clear();
            for (const auto& e : entries) entries_.insert(e.id, e);
            for (const auto& p : pending_) {
              if (p.deleting)
                entries_.remove(p.desired.id);
              else
                entries_.insert(p.desired.id, p.desired);
            }
            saveCache();
            emit reset();
          });
  connect(service, &sync::myanimelist::Service::mangaListFailed, this,
          [this] { refreshing_ = false; });
  connect(service, &sync::myanimelist::Service::mangaEntryUpdated, this,
          [this](const Entry& e) { complete(e.id, &e); });
  connect(service, &sync::myanimelist::Service::mangaEntryDeleted, this,
          [this](int id) { complete(id, nullptr); });
  connect(service, &sync::myanimelist::Service::mangaMutationFailed, this, [this](int id) {
    if (processing_ == id) {
      processing_ = 0;
      emit pendingChanged();
    }
  });
  connect(service, &sync::myanimelist::Service::mangaDetailsFetched, this,
          [this](const Entry& details) {
            if (!entries_.contains(details.id)) return;
            auto e = entries_.value(details.id);
            e.genres = details.genres;
            e.authors = details.authors;
            e.synopsis = details.synopsis;
            e.startDate = details.startDate;
            e.endDate = details.endDate;
            e.alternativeTitles = details.alternativeTitles;
            e.rank = details.rank;
            e.popularity = details.popularity;
            entries_.insert(e.id, e);
            saveCache();
            emit changed(e.id);
          });
  loadAccount();
  retry_.start();
}

void Library::loadAccount() {
  const auto username = QString::fromStdString(taiga::accounts.myanimelistUsername());
  if (username == account_ && (ready_ || epoch_)) return;
  account_ = username;
  ready_ = false;
  processing_ = 0;
  refreshing_ = false;
  entries_.clear();
  pending_.clear();
  emit reset();
  const auto epoch = ++epoch_;
  if (account_.isEmpty()) {
    ready_ = true;
    return;
  }
  const auto cache = path({}), queue = path(QStringLiteral("_pending"));
  QtConcurrent::run(&writer_, [cache, queue] {
    return std::pair{readJson(cache), readJson(queue)};
  }).then(this, [this, epoch](const std::pair<QJsonDocument, QJsonDocument>& docs) {
    if (epoch != epoch_) return;
    for (const auto& v : docs.first.array()) {
      auto e = fromJson(v.toObject());
      if (e.id > 0) entries_.insert(e.id, e);
    }
    for (const auto& v : docs.second.array()) {
      const auto o = v.toObject();
      Pending p{fromJson(o["desired"].toObject()), fromJson(o["previous"].toObject()),
                o["deleting"].toBool(), ++version_};
      if (p.desired.id <= 0) continue;
      pending_.insert(p.desired.id, p);
      if (p.deleting)
        entries_.remove(p.desired.id);
      else
        entries_.insert(p.desired.id, p.desired);
    }
    ready_ = true;
    emit reset();
    emit pendingChanged();
    process();
  });
}

void Library::saveCache() {
  if (account_.isEmpty()) return;
  const auto cache = path({});
  const auto snapshot = entries_;
  QtConcurrent::run(&writer_, [cache, snapshot] {
    QJsonArray rows;
    for (const auto& e : snapshot) rows.append(toJson(e));
    return writeJson(cache, QJsonDocument(rows));
  }).then(this, [this](bool ok) {
    if (!ok) emit errorOccurred(tr("Could not save the manga cache."));
  });
}

bool Library::persistPending() {
  QJsonArray rows;
  for (const auto& p : pending_)
    rows.append(QJsonObject{{"desired", toJson(p.desired)},
                            {"previous", toJson(p.previous)},
                            {"deleting", p.deleting}});
  if (writeJson(path(QStringLiteral("_pending")), QJsonDocument(rows))) return true;
  emit errorOccurred(tr("Could not save pending manga changes. The edit was not accepted."));
  return false;
}

bool Library::save(const Entry& value) {
  if (value.id <= 0 || !ready_ || account_.isEmpty()) return false;
  const auto old = pending_;
  auto e = value;
  e.onList = true;
  const auto previous = pending_.contains(e.id) ? pending_[e.id].previous : entries_.value(e.id);
  pending_.insert(e.id, {e, previous, false, ++version_});
  if (!persistPending()) {
    pending_ = old;
    return false;
  }
  entries_.insert(e.id, e);
  saveCache();
  emit changed(e.id);
  emit pendingChanged();
  process();
  return true;
}

bool Library::remove(int id) {
  if (!ready_ || !entries_.contains(id)) return false;
  const auto old = pending_;
  const auto e = entries_.value(id);
  pending_.insert(id, {e, e, true, ++version_});
  if (!persistPending()) {
    pending_ = old;
    return false;
  }
  entries_.remove(id);
  saveCache();
  emit changed(id);
  emit pendingChanged();
  process();
  return true;
}

void Library::process() {
  if (!ready_ || processing_ || pending_.isEmpty() || !taiga::settings.syncEnabled() ||
      account_ != QString::fromStdString(taiga::accounts.myanimelistUsername()) ||
      taiga::accounts.myanimelistAccessToken().empty())
    return;
  const auto p = pending_.cbegin().value();
  processing_ = p.desired.id;
  processingVersion_ = p.version;
  auto* service = sync::myanimelist::Service::instance();
  if (p.deleting)
    service->deleteMangaEntry(processing_);
  else
    service->updateMangaEntry(p.desired, p.previous);
}

void Library::complete(int id, const Entry* e) {
  if (processing_ != id || !pending_.contains(id)) return;
  const bool latest = pending_[id].version == processingVersion_;
  processing_ = 0;
  ++version_;  // a refresh started before this response must not overwrite it
  if (latest) {
    pending_.remove(id);
    if (e)
      entries_.insert(id, *e);
    else
      entries_.remove(id);
  } else {
    pending_[id].previous = e ? *e : Entry{};
  }
  persistPending();
  saveCache();
  emit changed(id);
  emit pendingChanged();
  process();
}

void Library::refresh() {
  init();
  if (!ready_) {
    QTimer::singleShot(100, this, &Library::refresh);
    return;
  }
  if (refreshing_) {
    process();
    return;
  }
  refreshVersion_ = version_;
  if (!account_.isEmpty()) {
    refreshing_ = true;
    sync::myanimelist::Service::instance()->fetchMangaList();
  }
  process();
}

}  // namespace manga
