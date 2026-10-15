#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <array>

namespace stats {

// A snapshot of account data, with scores normalized to 0..10. Unknown totals
// and durations stay zero; no release date is substituted for a user's date.
struct Record {
  int id = 0;
  QString title;
  QString status;
  QString format;
  QStringList genres;
  QStringList studios;
  QString releaseYear;
  QString releaseSeason;
  QString completionYear;
  QString completionMonth;
  QDate started;
  QDate completed;
  QDate updated;
  double score = 0;
  double communityScore = 0;
  qint64 progress = 0;
  qint64 total = 0;
  qint64 secondaryProgress = 0;  // manga volumes
  qint64 repeats = 0;
  int minutesPerEpisode = 0;
  bool estimatedDuration = false;
  bool repeating = false;
};

struct Filter {
  QString status, format, year, genre, studio;
  bool accepts(const Record& record) const;
};

enum class Dimension {
  Status,
  Format,
  Genre,
  Studio,
  ReleaseYear,
  ReleaseSeason,
  CompletionYear,
  CompletionMonth,
  Score,
  Length,
  ScoreDifference,
  Progress
};

struct Group {
  QString name;
  QList<int> ids;
  int scored = 0;
  double scoreSum = 0;
  qint64 units = 0;
  qint64 seconds = 0;
  double mean() const {
    return scored ? scoreSum / scored : 0;
  }
};

struct Summary {
  QList<Record> records;
  QMap<Dimension, QList<Group>> groups;
  int scored = 0, completed = 0, dropped = 0, repeatedTitles = 0;
  int activeRepeats = 0;
  int compared = 0, unknownTotals = 0, unknownDuration = 0;
  int estimatedDuration = 0, unknownCompletionDates = 0, stale = 0;
  qint64 units = 0, secondaryUnits = 0, repeatUnits = 0, repeatCount = 0;
  qint64 seconds = 0, remainingUnits = 0, remainingSeconds = 0;
  double mean = 0, median = 0, deviation = 0, meanDifference = 0;
  double communityMean = 0, meanAbsoluteDifference = 0, weightedMean = 0;
  qint64 ratedUnits = 0;
  int higher = 0, lower = 0, similar = 0;
  QList<double> modes;
};

Summary calculate(const QList<Record>& records, const Filter& filter = {},
                  QDate today = QDate::currentDate());
QStringList keys(const Record& record, Dimension dimension);

struct ActivityChange {
  qint64 id = 0;
  int mediaId = 0;
  QDateTime time;
  qint64 previousProgress = 0, progress = 0;
  double previousScore = 0, score = 0;
};
struct ActivitySummary {
  QList<ActivityChange> changes;
  QList<Group> days, months;
  QDate firstDate;
  qint64 addedUnits = 0;
  int scoreChanges = 0;
};
ActivitySummary calculateActivity(const QList<ActivityChange>& changes, const QSet<int>& ids);

}  // namespace stats
