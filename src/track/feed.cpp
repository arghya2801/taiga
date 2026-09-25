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

#include "feed.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QXmlStreamReader>
#include <cmath>

#include "base/string.hpp"
#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"
#include "taiga/network.hpp"
#include "taiga/options.hpp"
#include "taiga/path.hpp"
#include "track/episode.hpp"
#include "track/recognition.hpp"
#include "track/scanner.hpp"

namespace track::feed {

namespace {

constexpr int kArchiveLimit = 2000;

QString dataPath(const QString& name) {
  return u"%1/%2"_s.arg(QString::fromStdString(taiga::get_data_path()), name);
}

Context contextFor(const Item& item) {
  Context context;
  if (const auto anime = anime::db.item(item.animeId)) {
    context.episodeCount = anime->episode_count;
    context.airingStatus = static_cast<int>(anime::airingStatus(*anime));
    context.type = static_cast<int>(anime->type);
  }
  if (const auto entry = anime::db.entry(item.animeId); entry && !entry->pending_delete) {
    context.watched = entry->watched_episodes;
    context.listStatus = static_cast<int>(entry->status);
    context.notes = QString::fromStdString(entry->notes);
  }
  context.episodeAvailable = item.episode > 0 &&
                             track::availableEpisodes.contains(item.animeId, item.episode);
  return context;
}

// A folder for the anime, set in its settings, for the %folder% argument.
QString animeFolder(int animeId) {
  if (const auto settings = anime::db.settings(animeId)) {
    return QString::fromStdString(settings->folder);
  }
  return {};
}

}  // namespace

Aggregator* aggregator() {
  static auto instance = new Aggregator;
  return instance;
}

Aggregator::Aggregator() {
  QFile file{dataPath(u"torrent_archive.json"_s)};
  if (file.open(QIODevice::ReadOnly)) {
    for (const auto& value : QJsonDocument::fromJson(file.readAll()).array()) {
      m_archive.append(value.toString());
    }
  }
  connect(&m_timer, &QTimer::timeout, this, [this] { check(true); });
}

void Aggregator::check(bool automatic) {
  fetch(taiga::opt::torrentSource.get(), automatic);
}

void Aggregator::search(const QString& title) {
  auto url = taiga::opt::torrentSearchSource.get();
  url.replace(u"%title%"_s, QString::fromLatin1(QUrl::toPercentEncoding(title)));
  fetch(url, false);
}

void Aggregator::fetch(const QString& url, bool automatic) {
  if (m_busy || url.isEmpty()) return;
  m_busy = true;
  emit itemsChanged();

  QNetworkRequest request{QUrl{url}};
  request.setHeaders(taiga::NetworkAccessManager::commonHeaders());
  auto* reply = taiga::network()->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply, automatic] {
    m_busy = false;
    if (reply->error() != QNetworkReply::NoError) {
      emit errorOccurred(reply->errorString());
      emit itemsChanged();
      return;
    }
    m_unfiltered = parse(reply->readAll());
    refilter();

    if (automatic) {
      QList<Item> selected;
      for (const auto& item : m_items) {
        if (item.state == ItemState::Selected) selected.append(item);
      }
      if (!selected.isEmpty()) {
        if (taiga::opt::torrentNewAction.get() ==
            static_cast<int>(taiga::opt::TorrentNewAction::Download)) {
          for (const auto& item : selected) download(item);
        }
        emit newItemsFound(selected);
      }
    }
  });
}

QList<Item> Aggregator::parse(const QByteArray& data) const {
  QList<Item> items;
  QXmlStreamReader xml{data};

  while (!xml.atEnd()) {
    xml.readNext();
    if (!xml.isStartElement() || xml.name() != u"item") continue;

    Item item;
    while (xml.readNextStartElement()) {
      const auto name = xml.qualifiedName();
      if (name == u"enclosure") {
        if (item.link.isEmpty()) item.link = xml.attributes().value(u"url").toString();
        xml.skipCurrentElement();
        continue;
      }
      const auto text = xml.readElementText();
      if (name == u"title") {
        item.title = text;
      } else if (name == u"link") {
        item.link = text;
      } else if (name == u"guid") {
        item.guid = text;
      } else if (name == u"description") {
        item.description = text;
      } else if (name == u"pubDate") {
        item.date = QDateTime::fromString(text, Qt::RFC2822Date);
      } else if (name == u"category" || name == u"nyaa:category") {
        item.category = text;
      } else if (name == u"nyaa:size" || name == u"size") {
        // "1.2 GiB"; the filter parses the same format
        const auto parts = text.split(u' ');
        const double number = parts.value(0).toDouble();
        const auto unit = parts.value(1).left(1).toUpper();
        const int power = unit.isEmpty() ? 0 : QStringView{u"KMGT"}.indexOf(unit.front()) + 1;
        item.size = static_cast<qint64>(number * std::pow(1024.0, power));
      } else if (name == u"nyaa:infoHash") {
        item.magnet = u"magnet:?xt=urn:btih:%1&dn=%2"_s.arg(
            text, QString::fromLatin1(QUrl::toPercentEncoding(item.title)));
      }
    }
    if (item.link.startsWith(u"magnet:")) item.magnet = item.link;

    auto episode = recognition::parse(item.title.toStdString());
    item.animeTitle = QString::fromStdString(episode.element(anitomy::ElementKind::Title));
    item.group = QString::fromStdString(episode.element(anitomy::ElementKind::ReleaseGroup));
    item.resolution =
        QString::fromStdString(episode.element(anitomy::ElementKind::VideoResolution));
    item.version =
        QString::fromStdString(episode.element(anitomy::ElementKind::ReleaseVersion)).toInt();
    if (item.version < 1) item.version = 1;
    for (const auto& term : episode.elements(anitomy::ElementKind::VideoTerm)) {
      item.videoType += (item.videoType.isEmpty() ? u""_s : u" "_s) + QString::fromStdString(term);
    }
    item.animeId = recognition::identify(episode);
    if (const auto range = episode.episodeNumberRange()) item.episode = range->second;

    items.append(item);
  }

  return items;
}

void Aggregator::refilter() {
  m_items = m_unfiltered;
  if (taiga::opt::torrentFiltersEnabled.get()) {
    applyFilters(m_items, filters(), contextFor);
  }
  for (auto& item : m_items) {
    if (!item.isDiscarded() && m_archive.contains(item.title)) item.state = ItemState::Discarded;
  }
  emit itemsChanged();
}

void Aggregator::download(const Item& item) {
  const auto app = taiga::opt::torrentApp.get();

  const auto open = [&](const QString& target) {
    if (app.isEmpty()) {
      QDesktopServices::openUrl(target.startsWith(u"magnet:") ? QUrl{target}
                                                               : QUrl::fromLocalFile(target));
      return;
    }
    auto args = QProcess::splitCommand(taiga::opt::torrentAppArgs.get());
    for (auto& arg : args) {
      arg.replace(u"%file%"_s, target).replace(u"%folder%"_s, animeFolder(item.animeId));
    }
    args.removeAll(QString{});
    QProcess::startDetached(app, args);
  };

  archive(item);

  if (!item.magnet.isEmpty() && (item.link.isEmpty() || item.link.startsWith(u"magnet:"))) {
    open(item.magnet);
    return;
  }

  QNetworkRequest request{QUrl{item.link}};
  request.setHeaders(taiga::NetworkAccessManager::commonHeaders());
  auto* reply = taiga::network()->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply, item, open] {
    if (reply->error() != QNetworkReply::NoError) {
      // Fall back to the magnet link when the .torrent can't be fetched.
      if (!item.magnet.isEmpty()) {
        open(item.magnet);
      } else {
        emit errorOccurred(reply->errorString());
      }
      return;
    }
    auto name = item.title;
    name.replace(QRegularExpression{uR"([<>:"/\\|?*])"_s}, u"_"_s);
    const auto path = dataPath(u"torrents/%1.torrent"_s.arg(name));
    QDir{}.mkpath(QFileInfo{path}.path());
    QSaveFile file{path};
    if (file.open(QIODevice::WriteOnly)) {
      file.write(reply->readAll());
      file.commit();
      open(path);
    }
  });
}

void Aggregator::archive(const Item& item) {
  if (!m_archive.contains(item.title)) {
    m_archive.append(item.title);
    while (m_archive.size() > kArchiveLimit) m_archive.removeFirst();
    saveArchive();
  }
  for (auto& i : m_items) {
    if (i.title == item.title && !i.isDiscarded()) i.state = ItemState::Discarded;
  }
  emit itemsChanged();
}

void Aggregator::saveArchive() const {
  QSaveFile file{dataPath(u"torrent_archive.json"_s)};
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument{QJsonArray::fromStringList(m_archive)}.toJson(QJsonDocument::Compact));
    file.commit();
  }
}

QList<Filter> Aggregator::filters() const {
  const auto json = taiga::opt::torrentFilters.get();
  if (json.isEmpty()) return defaultFilters();
  return fromJson(QJsonDocument::fromJson(json.toUtf8()).array());
}

void Aggregator::setFilters(const QList<Filter>& filters) {
  taiga::opt::torrentFilters.set(
      QString::fromUtf8(QJsonDocument{toJson(filters)}.toJson(QJsonDocument::Compact)));
  refilter();
}

void Aggregator::updateTimer() {
  if (taiga::opt::torrentAutoCheck.get()) {
    m_timer.start(std::chrono::minutes{std::max(taiga::opt::torrentAutoCheckMinutes.get(), 5)});
  } else {
    m_timer.stop();
  }
}

}  // namespace track::feed
