#pragma once

#include <QString>
#include <QStringList>

namespace manga {

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
