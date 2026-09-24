#include "myanimelist.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRestReply>
#include <QUrlQuery>
#include <utility>

#include "base/string.hpp"
#include "sync/myanimelist/myanimelist_error.hpp"
#include "sync/myanimelist/myanimelist_utils.hpp"
#include "taiga/accounts.hpp"

namespace sync::myanimelist {

namespace {

constexpr int kMangaPageLimit = 1000;

manga::Entry parseManga(const QJsonObject& node, const QJsonObject& status = {}) {
  manga::Entry entry;
  entry.id = node["id"].toInt();
  entry.title = node["title"].toString();
  entry.type = node["media_type"].toString();
  entry.chapters = node["num_chapters"].toInt();
  entry.volumes = node["num_volumes"].toInt();
  entry.mean = node["mean"].toDouble();
  const auto listStatus = status.isEmpty() ? node["my_list_status"].toObject() : status;
  entry.onList = !listStatus.isEmpty();
  entry.status = listStatus["status"].toString();
  entry.chaptersRead = listStatus["num_chapters_read"].toInt();
  entry.volumesRead = listStatus["num_volumes_read"].toInt();
  entry.score = listStatus["score"].toInt();
  return entry;
}

QString mangaFields() {
  return u"id,title,media_type,num_chapters,num_volumes,mean,my_list_status"_s;
}

}  // namespace

void Service::fetchMangaList(int offset, QList<manga::Entry> entries) {
  const auto username = QString::fromStdString(taiga::accounts.myanimelistUsername());
  if (username.isEmpty()) {
    emit errorOccurred("Connect a MyAnimeList account in Settings first.");
    return;
  }

  const QUrlQuery query{{"limit", QString::number(kMangaPageLimit)},
                        {"offset", QString::number(offset)},
                        {"nsfw", "true"},
                        {"fields", u"%1,list_status{status,score,num_chapters_read,num_volumes_read}"_s.arg(mangaFields())}};
  const auto callback = [this, offset, entries = std::move(entries)](QRestReply& reply) mutable {
    if (isError(reply)) {
      if (retryOnTokenExpiry(reply, [this, offset, entries] { fetchMangaList(offset, entries); }))
        return;
      handleError(*this, reply, "Could not load manga list.");
      return;
    }
    const auto json = reply.readJson();
    if (!json) {
      handleError(*this, reply, "Could not parse manga list.");
      return;
    }
    const auto root = json->object();
    for (const auto& value : root["data"].toArray()) {
      const auto item = value.toObject();
      auto entry = parseManga(item["node"].toObject(), item["list_status"].toObject());
      entry.onList = true;
      if (entry.id > 0 && !entry.title.isEmpty()) entries.append(entry);
    }
    if (const auto next = pagingOffset(root["paging"].toObject(), u"next"_s)) {
      fetchMangaList(*next, std::move(entries));
    } else {
      emit mangaListFetched(entries);
    }
  };

  manager_.get(api_.createRequest(u"/users/%1/mangalist"_s.arg(username), query), this,
               callback);
}

void Service::searchManga(const QString& queryText) {
  if (queryText.trimmed().isEmpty()) return;
  const QUrlQuery query{{"q", queryText.trimmed()},
                        {"limit", "100"},
                        {"nsfw", "true"},
                        {"fields", mangaFields()}};
  const auto callback = [this, queryText](QRestReply& reply) {
    if (isError(reply)) {
      if (retryOnTokenExpiry(reply, [this, queryText] { searchManga(queryText); })) return;
      handleError(*this, reply, "Could not search manga.");
      return;
    }
    const auto json = reply.readJson();
    if (!json) {
      handleError(*this, reply, "Could not parse manga search results.");
      return;
    }
    QList<manga::Entry> entries;
    for (const auto& value : json->object()["data"].toArray()) {
      const auto entry = parseManga(value.toObject()["node"].toObject());
      if (entry.id > 0 && !entry.title.isEmpty()) entries.append(entry);
    }
    emit mangaSearchCompleted(queryText, entries);
  };

  manager_.get(api_.createRequest(u"/manga"_s, query), this, callback);
}

void Service::updateMangaEntry(const manga::Entry& entry) {
  QUrlQuery body{{"status", entry.status},
                 {"num_chapters_read", QString::number(entry.chaptersRead)},
                 {"num_volumes_read", QString::number(entry.volumesRead)},
                 {"score", QString::number(entry.score)}};
  auto request = api_.createRequest(u"/manga/%1/my_list_status"_s.arg(entry.id));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

  const auto callback = [this, entry](QRestReply& reply) {
    if (isError(reply)) {
      if (retryOnTokenExpiry(reply, [this, entry] { updateMangaEntry(entry); })) return;
      handleError(*this, reply, "Could not save manga entry.");
      return;
    }
    const auto json = reply.readJson();
    if (!json) {
      handleError(*this, reply, "Could not confirm manga update.");
      return;
    }
    const auto status = json->object();
    auto updated = entry;
    updated.status = status["status"].toString(entry.status);
    updated.chaptersRead = status["num_chapters_read"].toInt(entry.chaptersRead);
    updated.volumesRead = status["num_volumes_read"].toInt(entry.volumesRead);
    updated.score = status["score"].toInt(entry.score);
    updated.onList = true;
    emit mangaEntryUpdated(updated);
  };

  manager_.patch(request, formUrlEncode(body), this, callback);
}

void Service::deleteMangaEntry(int id) {
  const auto callback = [this, id](QRestReply& reply) {
    if (isError(reply) && reply.httpStatus() != 404) {
      if (retryOnTokenExpiry(reply, [this, id] { deleteMangaEntry(id); })) return;
      handleError(*this, reply, "Could not delete manga entry.");
      return;
    }
    emit mangaEntryDeleted(id);
  };
  manager_.deleteResource(api_.createRequest(u"/manga/%1/my_list_status"_s.arg(id)), this,
                          callback);
}

}  // namespace sync::myanimelist
