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

#include "image_provider.hpp"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QNetworkRequest>
#include <QPixmapCache>
#include <QRestReply>
#include <QUrl>
#include <QtConcurrentRun>

#include "base/string.hpp"
#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"
#include "sync/service.hpp"
#include "taiga/network.hpp"
#include "taiga/path.hpp"

namespace gui {

namespace {

constexpr int kPixmapCacheLimitKb = 200 * 1024;  // 200MB

// Posters are never drawn wider than this, so storing more only costs disk and decode time.
// ponytail: JPEG because it's built into Qt; switch to WebP if qtimageformats gets bundled.
constexpr int kMaxWidth = 400;
constexpr int kJpegQuality = 85;

QImage shrink(QImage image) {
  if (image.width() > kMaxWidth) {
    image = image.scaledToWidth(kMaxWidth, Qt::SmoothTransformation);
  }
  return image.convertToFormat(QImage::Format_RGB32);
}

bool saveImage(const QImage& image, const QString& path) {
  QDir().mkpath(QFileInfo(path).path());
  return image.save(path, "JPG", kJpegQuality);
}

QString cacheRoot() {
  return u"%1/cache"_s.arg(QString::fromStdString(taiga::get_data_path()));
}

}  // namespace

ImageProvider::ImageProvider() : QObject() {
  QPixmapCache::setCacheLimit(kPixmapCacheLimitKb);
}

void ImageProvider::init() {
  m_manager = new QRestAccessManager(taiga::network(), this);
}

void ImageProvider::fetchPoster(const int id) {
  if (const auto item = anime::db.item(id); item && !item->image_url.empty()) {
    fetch(Kind::Anime, id, QString::fromStdString(item->image_url), false);
  }
}

QPixmap ImageProvider::loadPoster(const int id) {
  const auto item = anime::db.item(id);
  if (!item) return {};
  return load(Kind::Anime, id, QString::fromStdString(item->image_url));
}

QPixmap ImageProvider::loadCover(const int mangaId, const QString& url) {
  return load(Kind::Manga, mangaId, url);
}

qint64 ImageProvider::cacheSize() const {
  qint64 size = 0;
  for (QDirIterator it{cacheRoot(), QDir::Files, QDirIterator::Subdirectories}; it.hasNext();) {
    size += it.nextFileInfo().size();
  }
  return size;
}

void ImageProvider::clearCache() {
  QDir{cacheRoot()}.removeRecursively();
  QPixmapCache::clear();
  m_retryAfter.clear();
}

QPixmap ImageProvider::load(const Kind kind, const int id, const QString& url) {
  const auto key = cacheKey(kind, id);

  if (QPixmap pixmap; QPixmapCache::find(key, &pixmap)) return pixmap;
  if (url.isEmpty() || m_loading.contains(key) || !canRetry(key)) return {};

  const auto path = fileName(kind, id);

  // Nothing on disk: go straight to the network instead of failing a read first.
  if (!QFile::exists(path)) {
    fetch(kind, id, url, false);
    return {};
  }

  m_loading.insert(key);

  QtConcurrent::run([path] {
    QImage image{path};
    // Files cached before downscaling was added get shrunk the first time they're read.
    if (image.width() > kMaxWidth) {
      image = shrink(image);
      saveImage(image, path);
    }
    return image;
  }).then(this, [this, kind, id, key, url](const QImage& image) {
    m_loading.remove(key);
    if (image.isNull()) {
      QFile::remove(fileName(kind, id));
      fetch(kind, id, url, false);
      return;
    }
    QPixmapCache::insert(key, QPixmap::fromImage(image));
    if (kind == Kind::Anime && isStale(id)) fetch(kind, id, url, true);
    emitChanged(kind, id);
  });

  return {};
}

void ImageProvider::fetch(const Kind kind, const int id, const QString& url,
                          const bool revalidate) {
  const auto key = cacheKey(kind, id);
  const auto path = fileName(kind, id);

  QNetworkRequest request{QUrl{url}};
  if (revalidate) {
    request.setHeader(QNetworkRequest::IfModifiedSinceHeader, QFileInfo{path}.lastModified());
  }

  m_loading.insert(key);

  m_manager->get(request, this, [this, kind, id, key, path](QRestReply& reply) {
    if (reply.httpStatus() == 304) {
      // Mark the file fresh so the next load doesn't revalidate again.
      QFile file{path};
      if (file.open(QIODevice::ReadWrite)) {
        file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
      }
      m_loading.remove(key);
      return;
    }

    if (!reply.isHttpStatusSuccess() || reply.hasError()) {
      m_loading.remove(key);
      retryAfter(key);
      if (kind == Kind::Anime && reply.httpStatus() == 404) {
        if (const auto item = anime::db.item(id)) {
          auto updatedItem = *item;
          updatedItem.image_url.clear();
          anime::db.updateItem(updatedItem);
        }
      }
      return;
    }

    // Decode, shrink and encode off the UI thread.
    QtConcurrent::run([body = reply.readBody(), path] {
      auto image = QImage::fromData(body);
      if (!image.isNull()) {
        image = shrink(image);
        saveImage(image, path);
      }
      return image;
    }).then(this, [this, kind, id, key](const QImage& image) {
      m_loading.remove(key);
      if (image.isNull()) {
        retryAfter(key);
        return;
      }
      m_retryAfter.remove(key);
      QPixmapCache::insert(key, QPixmap::fromImage(image));
      emitChanged(kind, id);
    });
  });
}

void ImageProvider::emitChanged(const Kind kind, const int id) {
  if (kind == Kind::Anime) {
    emit posterChanged(id);
  } else {
    emit coverChanged(id);
  }
}

QString ImageProvider::cacheKey(const Kind kind, const int id) const {
  return u"%1/%2"_s.arg(kind == Kind::Anime ? u"poster"_s : u"cover"_s).arg(id);
}

QString ImageProvider::fileName(const Kind kind, const int id) const {
  if (kind == Kind::Manga) {
    return u"%1/myanimelist/manga/%2.jpg"_s.arg(cacheRoot()).arg(id);
  }
  const auto service = sync::serviceSlug(sync::currentServiceId());
  return u"%1/%2/media/%3.jpg"_s.arg(cacheRoot()).arg(service).arg(id);
}

bool ImageProvider::isStale(const int id) const {
  constexpr int kStaleDays = 7;

  const auto item = anime::db.item(id);

  if (!item) return false;

  if (anime::airingStatus(*item) == anime::Status::FinishedAiring) {
    return false;
  }

  const QFileInfo file{fileName(Kind::Anime, id)};
  return file.lastModified().daysTo(QDateTime::currentDateTime()) >= kStaleDays;
}

bool ImageProvider::canRetry(const QString& key) const {
  const auto it = m_retryAfter.find(key);
  return it == m_retryAfter.end() || QDateTime::currentDateTime() >= it.value();
}

void ImageProvider::retryAfter(const QString& key) {
  constexpr int kRetryCooldownSecs = 60;
  m_retryAfter[key] = QDateTime::currentDateTime().addSecs(kRetryCooldownSecs);
}

}  // namespace gui
