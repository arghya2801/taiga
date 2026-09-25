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

#include "sharing.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QtEndian>

#include "base/log.hpp"
#include "base/string.hpp"
#include "gui/utils/rating.hpp"
#include "media/anime_db.hpp"
#include "media/anime_utils.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/network.hpp"
#include "taiga/options.hpp"

namespace taiga::sharing {

namespace {

// Discord's local RPC: frames of [opcode][length][JSON] over the `discord-ipc-N` pipe.
// This is all the discord-rpc library did for us in v1.
class DiscordClient final : public QObject {
public:
  DiscordClient() {
    connect(&socket_, &QLocalSocket::connected, this, [this] {
      send(kHandshake, {{"v", 1}, {"client_id", opt::discordApplicationId.get()}});
    });
    connect(&socket_, &QLocalSocket::readyRead, this, [this] {
      socket_.readAll();  // the first reply is READY; nothing else is needed from Discord
      if (!ready_) {
        ready_ = true;
        flush();
      }
    });
    connect(&socket_, &QLocalSocket::disconnected, this, [this] { ready_ = false; });
    connect(&socket_, &QLocalSocket::errorOccurred, this, [this] {
      ready_ = false;
      // Discord listens on the first free pipe of ten. Deferred, as the socket is still
      // inside its error handling here.
      if (++pipe_ < 10) {
        QTimer::singleShot(0, this, [this] {
          if (socket_.state() == QLocalSocket::UnconnectedState) {
            socket_.connectToServer(u"discord-ipc-%1"_s.arg(pipe_));
          }
        });
      } else {
        pipe_ = 0;
      }
    });
  }

  // An empty activity clears it.
  void setActivity(const QJsonObject& activity) {
    pending_ = activity;
    if (ready_) {
      flush();
    } else if (socket_.state() == QLocalSocket::UnconnectedState && !activity.isEmpty()) {
      pipe_ = 0;
      socket_.connectToServer(u"discord-ipc-0"_s);
    }
  }

private:
  static constexpr qint32 kHandshake = 0;
  static constexpr qint32 kFrame = 1;

  void flush() {
    if (!pending_) return;
    QJsonObject args{{"pid", QCoreApplication::applicationPid()}};
    if (!pending_->isEmpty()) args["activity"] = *pending_;
    send(kFrame, {{"cmd", "SET_ACTIVITY"},
                  {"args", args},
                  {"nonce", QUuid::createUuid().toString(QUuid::WithoutBraces)}});
    pending_.reset();
  }

  void send(qint32 opcode, const QJsonObject& payload) {
    const auto json = QJsonDocument{payload}.toJson(QJsonDocument::Compact);
    QByteArray header(8, Qt::Uninitialized);
    qToLittleEndian(opcode, header.data());
    qToLittleEndian(static_cast<qint32>(json.size()), header.data() + 4);
    socket_.write(header + json);
  }

  QLocalSocket socket_;
  std::optional<QJsonObject> pending_;
  bool ready_ = false;
  int pipe_ = 0;
};

DiscordClient* discord() {
  static auto client = new DiscordClient;
  return client;
}

// Discord rejects strings shorter than 2 or longer than 128 characters.
QString limit(QString text) {
  if (text.size() > 128) text = text.left(127) + u'…';
  return text.size() < 2 ? QString{} : text;
}

QJsonObject discordActivity(const track::Episode& episode) {
  const auto item = anime::db.item(episode.animeId());
  if (!item) return {};

  const auto title = QString::fromStdString(anime::preferredTitle(*item));
  const auto number = QString::fromStdString(episode.element(anitomy::ElementKind::Episode));
  const auto group = QString::fromStdString(episode.element(anitomy::ElementKind::ReleaseGroup));

  QString state;
  if (!number.isEmpty()) {
    state = u"Episode %1"_s.arg(number);
    if (item->episode_count > 0) state += u"/%1"_s.arg(item->episode_count);
  }
  if (opt::discordShowGroup.get() && !group.isEmpty()) {
    state += (state.isEmpty() ? u"by %1"_s : u" by %1"_s).arg(group);
  }

  const auto service = sync::currentServiceId();
  const auto serviceName = sync::serviceName(service);
  const auto username =
      QString::fromStdString(accounts.serviceUsername(sync::serviceSlug(service).toStdString()));

  QJsonObject assets{
      {"large_image", item->image_url.empty() ? u"default"_s
                                              : QString::fromStdString(item->image_url)},
      {"large_text", limit(title)},
      {"small_image", sync::serviceSlug(service)},
      {"small_text", opt::discordShowUsername.get() && !username.isEmpty()
                         ? u"%1 at %2"_s.arg(username, serviceName)
                         : serviceName},
  };

  QJsonObject activity{
      {"type", 3},  // "Watching"
      {"details", limit(title)},
      {"assets", assets},
      {"buttons", QJsonArray{QJsonObject{{"label", "View Anime"},
                                         {"url", sync::animePageUrl(item->id)}}}},
  };
  if (const auto text = limit(state); !text.isEmpty()) activity["state"] = text;
  if (opt::discordShowTime.get()) {
    activity["timestamps"] = QJsonObject{{"start", QDateTime::currentSecsSinceEpoch()}};
  }
  return activity;
}

void postHttp(const QString& body) {
  const auto url = opt::httpUrl.get();
  if (url.isEmpty() || body.isEmpty()) return;
  QNetworkRequest request{QUrl{url}};
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  taiga::network()->post(request, body.toUtf8());
}

}  // namespace

atf::Fields fields(const std::optional<track::Episode>& episode, bool urlEncode) {
  atf::Fields fields;
  const auto set = [&](const char* name, const QString& value) {
    fields[QString::fromLatin1(name)] =
        urlEncode ? QString::fromLatin1(QUrl::toPercentEncoding(value)) : value;
  };

  const auto service = sync::currentServiceId();
  set("user", QString::fromStdString(
                  accounts.serviceUsername(sync::serviceSlug(service).toStdString())));
  set("playstatus", episode ? u"playing"_s : u"stopped"_s);
  if (!episode) return fields;

  const auto element = [&](anitomy::ElementKind kind) {
    return QString::fromStdString(episode->element(kind));
  };
  set("episode", element(anitomy::ElementKind::Episode));
  set("group", element(anitomy::ElementKind::ReleaseGroup));
  set("resolution", element(anitomy::ElementKind::VideoResolution));
  set("title", element(anitomy::ElementKind::Title));

  if (const auto item = anime::db.item(episode->animeId())) {
    set("id", QString::number(item->id));
    set("title", QString::fromStdString(anime::preferredTitle(*item)));
    set("total", item->episode_count > 0 ? QString::number(item->episode_count) : QString{});
    set("image", QString::fromStdString(item->image_url));
    set("animeurl", sync::animePageUrl(item->id));
    if (const auto entry = anime::db.entry(item->id)) {
      set("watched", entry->watched_episodes ? QString::number(entry->watched_episodes)
                                             : QString{});
      set("score", gui::formatRating(entry->score, {}));
      set("notes", QString::fromStdString(entry->notes));
      set("rewatching", entry->rewatching ? u"true"_s : QString{});
    }
  }
  return fields;
}

void clear() {
  discord()->setActivity({});
}

void announce(const std::optional<track::Episode>& episode) {
  if (!opt::sharingEnabled.get()) return;

  // Private entries stay private, like in v1.
  if (episode) {
    if (const auto entry = anime::db.entry(episode->animeId()); entry && entry->is_private) return;
  }

  if (opt::discordEnabled.get()) {
    discord()->setActivity(episode ? discordActivity(*episode) : QJsonObject{});
  }

  if (opt::httpEnabled.get()) {
    postHttp(atf::replace(opt::httpFormat.get(), fields(episode, true)));
  }
}

}  // namespace taiga::sharing
