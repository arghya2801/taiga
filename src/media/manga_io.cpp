#include <QJsonArray>
#include <QSaveFile>
#include <QXmlStreamWriter>

#include "manga_library.hpp"
namespace manga {
QJsonObject toJson(const Entry& e) {
  return {{"id", e.id},
          {"title", e.title},
          {"type", e.type},
          {"coverUrl", e.coverUrl},
          {"status", e.status},
          {"chapters", e.chapters},
          {"volumes", e.volumes},
          {"chaptersRead", e.chaptersRead},
          {"volumesRead", e.volumesRead},
          {"score", e.score},
          {"rereading", e.rereading},
          {"timesReread", e.timesReread},
          {"rereadValue", e.rereadValue},
          {"priority", e.priority},
          {"comments", e.comments},
          {"tags", QJsonArray::fromStringList(e.tags)},
          {"mean", e.mean},
          {"publicationStatus", e.publicationStatus},
          {"startedReading", e.startedReading},
          {"finishedReading", e.finishedReading},
          {"updatedAt", e.updatedAt},
          {"onList", e.onList},
          {"startDate", e.startDate},
          {"endDate", e.endDate},
          {"synopsis", e.synopsis},
          {"genres", QJsonArray::fromStringList(e.genres)},
          {"authors", QJsonArray::fromStringList(e.authors)},
          {"alternativeTitles", QJsonArray::fromStringList(e.alternativeTitles)},
          {"rank", e.rank},
          {"popularity", e.popularity}};
}

Entry fromJson(const QJsonObject& o) {
  Entry e;
  e.id = o["id"].toInt();
  e.title = o["title"].toString();
  e.type = o["type"].toString();
  e.coverUrl = o["coverUrl"].toString();
  e.status = o["status"].toString();
  e.chapters = o["chapters"].toInt();
  e.volumes = o["volumes"].toInt();
  e.chaptersRead = o["chaptersRead"].toInt();
  e.volumesRead = o["volumesRead"].toInt();
  e.score = o["score"].toInt();
  e.rereading = o["rereading"].toBool();
  e.timesReread = o["timesReread"].toInt();
  e.rereadValue = o["rereadValue"].toInt();
  e.priority = o["priority"].toInt();
  e.comments = o["comments"].toString();
  e.mean = o["mean"].toDouble();
  e.publicationStatus = o["publicationStatus"].toString();
  e.startedReading = o["startedReading"].toString();
  e.finishedReading = o["finishedReading"].toString();
  if (e.startedReading.startsWith("0000")) e.startedReading.clear();
  if (e.finishedReading.startsWith("0000")) e.finishedReading.clear();
  e.updatedAt = o["updatedAt"].toString();
  e.onList = o["onList"].toBool(true);
  e.startDate = o["startDate"].toString();
  e.endDate = o["endDate"].toString();
  e.synopsis = o["synopsis"].toString();
  e.rank = o["rank"].toInt();
  e.popularity = o["popularity"].toInt();
  for (const auto& v : o["tags"].toArray()) e.tags.append(v.toString());
  for (const auto& v : o["genres"].toArray()) e.genres.append(v.toString());
  for (const auto& v : o["authors"].toArray()) e.authors.append(v.toString());
  for (const auto& v : o["alternativeTitles"].toArray()) e.alternativeTitles.append(v.toString());
  return e;
}

bool exportList(const QList<Entry>& entries, const QString& path, bool xml) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return false;
  if (xml) {
    QXmlStreamWriter w(&file);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("myanimelist"));
    for (const auto& e : entries) {
      w.writeStartElement(QStringLiteral("manga"));
      const auto text = [&](const char* key, const QString& value) {
        w.writeTextElement(QLatin1String(key), value);
      };
      text("manga_mangadb_id", QString::number(e.id));
      text("manga_title", e.title);
      text("manga_chapters", QString::number(e.chapters));
      text("manga_volumes", QString::number(e.volumes));
      text("my_read_chapters", QString::number(e.chaptersRead));
      text("my_read_volumes", QString::number(e.volumesRead));
      text("my_score", QString::number(e.score));
      QString status = e.status;
      for (const auto& [key, label] : kStatuses)
        if (status == QLatin1String(key)) status = QLatin1String(label);
      if (e.status == "on_hold") status = QStringLiteral("On-Hold");
      if (e.status == "plan_to_read") status = QStringLiteral("Plan to Read");
      text("my_status", status);
      text("my_start_date",
           e.startedReading.isEmpty() ? QStringLiteral("0000-00-00") : e.startedReading);
      text("my_finish_date",
           e.finishedReading.isEmpty() ? QStringLiteral("0000-00-00") : e.finishedReading);
      text("my_comments", e.comments);
      text("my_tags", e.tags.join(','));
      text("my_times_read", QString::number(e.timesReread));
      text("my_reread_value", QString::number(e.rereadValue));
      text("update_on_import", QStringLiteral("1"));
      text("my_rereading", e.rereading ? QStringLiteral("1") : QStringLiteral("0"));
      w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndDocument();
    if (w.hasError()) return false;
  } else {
    QByteArray text("# Manga list\n\n");
    for (const auto& e : entries)
      text += QStringLiteral("- %1 (%2/%3 chapters, %4/10, %5)\n")
                  .arg(e.title)
                  .arg(e.chaptersRead)
                  .arg(e.chapters > 0 ? QString::number(e.chapters) : QStringLiteral("?"))
                  .arg(e.score)
                  .arg(e.status)
                  .toUtf8();
    if (file.write(text) != text.size()) return false;
  }
  return file.commit();
}

}  // namespace manga
