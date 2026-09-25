#pragma once

#include <QString>
#include <QStringList>

#include <utility>

namespace manga {

// MyAnimeList status values and their display labels, in list order.
constexpr std::pair<const char*, const char*> kStatuses[] = {
    {"reading", "Reading"}, {"completed", "Completed"},        {"on_hold", "On hold"},
    {"dropped", "Dropped"}, {"plan_to_read", "Plan to read"},
};

struct Entry {
  int id = 0;
  QString title;
  QString type;
  QString status;
  QString coverUrl;
  QString publicationStatus;
  QString startDate;
  QString endDate;
  QString synopsis;
  QStringList alternativeTitles;
  QStringList authors;
  QStringList genres;
  QStringList tags;
  QString comments;
  QString startedReading;  // list dates, "YYYY-MM-DD"
  QString finishedReading;
  QString updatedAt;  // ISO 8601
  int chapters = 0;
  int volumes = 0;
  int chaptersRead = 0;
  int volumesRead = 0;
  int score = 0;
  int rank = 0;
  int popularity = 0;
  int priority = 0;
  int timesReread = 0;
  int rereadValue = 0;
  double mean = 0.0;
  bool rereading = false;
  bool onList = false;
};

}  // namespace manga
