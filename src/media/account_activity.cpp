#include "account_activity.hpp"

#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <cmath>

#include "taiga/accounts.hpp"
#include "taiga/settings.hpp"

namespace stats {
QString ActivityLog::account(MediaKind kind) const {
  const auto service =
      kind == MediaKind::Manga ? std::string("myanimelist") : taiga::settings.service();
  const auto username = taiga::accounts.serviceUsername(service);
  return username.empty() ? QString{} : QString::fromStdString(service + ":" + username);
}

bool ActivityLog::init() {
  if (!QSqlDatabase::database().isOpen()) return false;
  QSqlQuery query;
  return query.exec(
             "CREATE TABLE IF NOT EXISTS account_activity ("
             "id INTEGER PRIMARY KEY, account TEXT NOT NULL, kind INTEGER NOT NULL, media_id "
             "INTEGER NOT NULL, "
             "time INTEGER NOT NULL, previous_progress INTEGER NOT NULL, progress INTEGER NOT "
             "NULL, "
             "previous_score REAL NOT NULL, score REAL NOT NULL)") &&
         query.exec(
             "CREATE INDEX IF NOT EXISTS account_activity_scope ON account_activity(account, kind, "
             "id)");
}

bool ActivityLog::record(MediaKind kind, int id, qint64 previousProgress, qint64 progress,
                         double previousScore, double score) {
  if (previousProgress == progress && previousScore == score) return true;
  const auto owner = account(kind);
  if (owner.isEmpty() || id <= 0 || previousProgress < 0 || progress < 0 ||
      !std::isfinite(previousScore) || !std::isfinite(score) || previousScore < 0 ||
      previousScore > 10 || score < 0 || score > 10 || !init())
    return false;
  QSqlQuery query;
  query.prepare(
      "INSERT INTO "
      "account_activity(account,kind,media_id,time,previous_progress,progress,previous_score,score)"
      " "
      "VALUES(:account,:kind,:id,:time,:previousProgress,:progress,:previousScore,:score)");
  const auto now = QDateTime::currentDateTime();
  query.bindValue(":account", owner);
  query.bindValue(":kind", int(kind));
  query.bindValue(":id", id);
  query.bindValue(":time", now.toMSecsSinceEpoch());
  query.bindValue(":previousProgress", previousProgress);
  query.bindValue(":progress", progress);
  query.bindValue(":previousScore", previousScore);
  query.bindValue(":score", score);
  if (!query.exec()) {
    qWarning() << "Could not save account activity:" << query.lastError().text();
    return false;
  }
  const int index = int(kind);
  if (accounts_[index] == owner)
    records_[index].append({query.lastInsertId().toLongLong(), id, now, previousProgress, progress,
                            previousScore, score});
  emit changed();
  return true;
}

const QList<ActivityChange>& ActivityLog::records(MediaKind kind) {
  const int index = int(kind);
  const auto owner = account(kind);
  if (accounts_[index] == owner) return records_[index];
  records_[index].clear();
  accounts_[index].clear();
  if (owner.isEmpty() || !init()) return records_[index];
  QSqlQuery query;
  query.prepare("SELECT * FROM account_activity WHERE account=:account AND kind=:kind ORDER BY id");
  query.bindValue(":account", owner);
  query.bindValue(":kind", index);
  if (!query.exec()) return records_[index];
  while (query.next())
    records_[index].append(
        {query.value("id").toLongLong(), query.value("media_id").toInt(),
         QDateTime::fromMSecsSinceEpoch(query.value("time").toLongLong()),
         query.value("previous_progress").toLongLong(), query.value("progress").toLongLong(),
         query.value("previous_score").toDouble(), query.value("score").toDouble()});
  accounts_[index] = owner;
  return records_[index];
}
}  // namespace stats
