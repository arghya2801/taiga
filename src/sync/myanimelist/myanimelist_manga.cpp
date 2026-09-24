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
  const auto picture = node["main_picture"].toObject();
  entry.coverUrl = picture["large"].toString(picture["medium"].toString());
  entry.publicationStatus = node["status"].toString();
  entry.startDate = node["start_date"].toString();
  entry.endDate = node["end_date"].toString();
  entry.synopsis = node["synopsis"].toString();
  entry.rank = node["rank"].toInt();
  entry.popularity = node["popularity"].toInt();
  entry.chapters = node["num_chapters"].toInt();
  entry.volumes = node["num_volumes"].toInt();
  entry.mean = node["mean"].toDouble();
  const auto titles = node["alternative_titles"].toObject();
  for (const auto& title : {titles["en"].toString(), titles["ja"].toString()}) {
    if (!title.isEmpty() && title != entry.title && !entry.alternativeTitles.contains(title))
      entry.alternativeTitles.append(title);
  }
  for (const auto& value : titles["synonyms"].toArray()) {
    const auto title = value.toString();
    if (!title.isEmpty() && title != entry.title && !entry.alternativeTitles.contains(title))
      entry.alternativeTitles.append(title);
  }
  for (const auto& value : node["genres"].toArray()) {
    const auto genre = value.toObject()["name"].toString();
    if (!genre.isEmpty()) entry.genres.append(genre);
  }
  for (const auto& value : node["authors"].toArray()) {
    const auto author = value.toObject();
    const auto person = author["node"].toObject();
    const auto name = QStringList{person["first_name"].toString(), person["last_name"].toString()}
                          .join(' ').trimmed();
    if (!name.isEmpty()) entry.authors.append(name);
  }
  const auto listStatus = status.isEmpty() ? node["my_list_status"].toObject() : status;
  entry.onList = !listStatus.isEmpty();
  entry.status = listStatus["status"].toString();
  entry.chaptersRead = listStatus["num_chapters_read"].toInt();
  entry.volumesRead = listStatus["num_volumes_read"].toInt();
  entry.score = listStatus["score"].toInt();
  entry.rereading = listStatus["is_rereading"].toBool();
  entry.timesReread = listStatus["num_times_reread"].toInt();
  entry.rereadValue = listStatus["reread_value"].toInt();
  entry.priority = listStatus["priority"].toInt();
  entry.comments = listStatus["comments"].toString();
  for (const auto& value : listStatus["tags"].toArray()) {
    const auto tag = value.toString();
    if (!tag.isEmpty()) entry.tags.append(tag);
  }
  return entry;
}

QString mangaFields() {
  return u"id,title,main_picture,media_type,num_chapters,num_volumes,mean"_s;
}

QString mangaListFields() {
  return u"status,score,num_chapters_read,num_volumes_read,is_rereading,num_times_reread,reread_value,priority,tags,comments"_s;
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
                        {"fields", u"%1,list_status{%2}"_s.arg(mangaFields(), mangaListFields())}};
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
                        {"fields", u"%1,my_list_status{%2}"_s.arg(mangaFields(), mangaListFields())}};
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

void Service::fetchMangaDetails(int id) {
  const auto fields = u"%1,alternative_titles,start_date,end_date,synopsis,rank,popularity,genres,authors{first_name,last_name},status,my_list_status{%2}"_s
                          .arg(mangaFields(), mangaListFields());
  const QUrlQuery query{{"fields", fields}};
  const auto callback = [this, id](QRestReply& reply) {
    if (isError(reply)) {
      if (retryOnTokenExpiry(reply, [this, id] { fetchMangaDetails(id); })) return;
      handleError(*this, reply, "Could not load manga details.");
      emit mangaDetailsFailed(id);
      return;
    }
    const auto json = reply.readJson();
    if (!json) {
      handleError(*this, reply, "Could not parse manga details.");
      emit mangaDetailsFailed(id);
      return;
    }
    emit mangaDetailsFetched(parseManga(json->object()));
  };
  manager_.get(api_.createRequest(u"/manga/%1"_s.arg(id), query), this, callback);
}

void Service::updateMangaEntry(const manga::Entry& entry, const manga::Entry& previous) {
  const bool adding = !previous.onList;
  QUrlQuery body;
  if (adding || entry.status != previous.status) body.addQueryItem("status", entry.status);
  if (adding || entry.chaptersRead != previous.chaptersRead)
    body.addQueryItem("num_chapters_read", QString::number(entry.chaptersRead));
  if (adding || entry.volumesRead != previous.volumesRead)
    body.addQueryItem("num_volumes_read", QString::number(entry.volumesRead));
  if (adding || entry.score != previous.score)
    body.addQueryItem("score", QString::number(entry.score));
  if (adding || entry.rereading != previous.rereading)
    body.addQueryItem("is_rereading", entry.rereading ? "true" : "false");
  if (adding || entry.timesReread != previous.timesReread)
    body.addQueryItem("num_times_reread", QString::number(entry.timesReread));
  if (adding || entry.rereadValue != previous.rereadValue)
    body.addQueryItem("reread_value", QString::number(entry.rereadValue));
  if (adding || entry.priority != previous.priority)
    body.addQueryItem("priority", QString::number(entry.priority));
  if (adding || entry.tags != previous.tags) body.addQueryItem("tags", entry.tags.join(','));
  if (adding || entry.comments != previous.comments) body.addQueryItem("comments", entry.comments);
  auto request = api_.createRequest(u"/manga/%1/my_list_status"_s.arg(entry.id));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

  const auto callback = [this, entry, previous](QRestReply& reply) {
    if (isError(reply)) {
      if (retryOnTokenExpiry(reply, [this, entry, previous] {
            updateMangaEntry(entry, previous);
          })) return;
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
    updated.rereading = status["is_rereading"].toBool(entry.rereading);
    updated.timesReread = status["num_times_reread"].toInt(entry.timesReread);
    updated.rereadValue = status["reread_value"].toInt(entry.rereadValue);
    updated.priority = status["priority"].toInt(entry.priority);
    updated.comments = status["comments"].toString(entry.comments);
    if (status.contains("tags")) {
      updated.tags.clear();
      for (const auto& value : status["tags"].toArray()) updated.tags.append(value.toString());
    }
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
