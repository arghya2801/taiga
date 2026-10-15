#include "account_stats.hpp"

#include <QSet>
#include <algorithm>
#include <cmath>

namespace stats {

bool Filter::accepts(const Record& r) const {
  return (status.isEmpty() || r.status == status) && (format.isEmpty() || r.format == format) &&
         (year.isEmpty() || r.releaseYear == year) &&
         (genre.isEmpty() || r.genres.contains(genre)) &&
         (studio.isEmpty() || r.studios.contains(studio));
}

QStringList keys(const Record& r, Dimension d) {
  QStringList result;
  switch (d) {
    case Dimension::Status:
      result = {r.status};
      break;
    case Dimension::Format:
      result = {r.format};
      break;
    case Dimension::Genre:
      result = r.genres;
      break;
    case Dimension::Studio:
      result = r.studios;
      break;
    case Dimension::ReleaseYear:
      result = {r.releaseYear};
      break;
    case Dimension::ReleaseSeason:
      result = {r.releaseSeason};
      break;
    case Dimension::CompletionYear:
      result = {r.completionYear};
      break;
    case Dimension::CompletionMonth:
      result = {r.completionMonth};
      break;
    case Dimension::Score:
      result = {r.score > 0 ? QString::number(r.score, 'f', 1) : QStringLiteral("Unrated")};
      break;
    case Dimension::Length:
      if (r.total <= 0)
        result = {"Unknown"};
      else if (r.total == 1)
        result = {"1"};
      else if (r.total <= 6)
        result = {"2-6"};
      else if (r.total <= 16)
        result = {"7-16"};
      else if (r.total <= 28)
        result = {"17-28"};
      else if (r.total <= 55)
        result = {"29-55"};
      else if (r.total <= 100)
        result = {"56-100"};
      else
        result = {"101+"};
      break;
    case Dimension::ScoreDifference:
      result = {QString::number(std::clamp(qRound(r.score - r.communityScore), -4, 4))};
      break;
    case Dimension::Progress:
      result = {QString::number(
          int(std::clamp(4.0 * r.progress / std::max<qint64>(1, r.total), 0.0, 3.0)))};
      break;
  }
  result.removeAll(QString{});
  result.removeDuplicates();
  if (result.isEmpty()) result.append(QStringLiteral("Unknown"));
  return result;
}

Summary calculate(const QList<Record>& records, const Filter& filter, QDate today) {
  Summary s;
  QMap<Dimension, QMap<QString, Group>> groups;
  QList<double> scores;
  QMap<double, int> frequencies;
  QSet<int> seen;
  double squares = 0;
  for (auto r : records) {
    if (r.id <= 0 || seen.contains(r.id) || !filter.accepts(r)) continue;
    seen.insert(r.id);
    r.progress = std::max<qint64>(0, r.progress);
    r.total = std::max<qint64>(0, r.total);
    r.repeats = std::max<qint64>(0, r.repeats);
    if (!std::isfinite(r.score) || r.score < 0 || r.score > 10) r.score = 0;
    if (!std::isfinite(r.communityScore) || r.communityScore < 0 || r.communityScore > 10)
      r.communityScore = 0;
    s.records.append(r);
    const auto completedRepeatUnits = r.repeats * r.total;
    const auto repeated = completedRepeatUnits + (r.repeating ? r.progress : 0);
    // Resetting progress for another pass must preserve the original completed pass.
    const auto units = r.progress + completedRepeatUnits + (r.repeating ? r.total : 0);
    const auto seconds = units * std::max(r.minutesPerEpisode, 0) * 60;
    s.units += units;
    s.repeatUnits += repeated;
    s.repeatCount += r.repeats;
    s.secondaryUnits += std::max<qint64>(0, r.secondaryProgress);
    s.seconds += seconds;
    s.repeatedTitles += r.repeats > 0 || r.repeating;
    s.activeRepeats += r.repeating;
    s.completed += r.status == "Completed";
    s.dropped += r.status == "Dropped";
    s.unknownTotals += r.total <= 0;
    s.unknownDuration += r.minutesPerEpisode <= 0;
    s.estimatedDuration += r.estimatedDuration;
    s.unknownCompletionDates += r.status == "Completed" && r.completionYear.isEmpty();
    const bool backlog = r.repeating || (r.status != "Completed" && r.status != "Dropped");
    if (backlog && r.total > 0) {
      const auto remaining = std::max<qint64>(0, r.total - r.progress);
      s.remainingUnits += remaining;
      s.remainingSeconds += remaining * std::max(r.minutesPerEpisode, 0) * 60;
    }
    s.stale += backlog && r.updated.isValid() && r.updated.daysTo(today) >= 180;
    if (r.score > 0) {
      scores.append(r.score);
      ++frequencies[r.score];
      ++s.scored;
      const double delta = r.score - s.mean;
      s.mean += delta / s.scored;
      squares += delta * (r.score - s.mean);
      s.ratedUnits += units;
      s.weightedMean += r.score * units;
      if (r.communityScore > 0) {
        ++s.compared;
        const double difference = r.score - r.communityScore;
        s.meanDifference += (difference - s.meanDifference) / s.compared;
        s.communityMean += (r.communityScore - s.communityMean) / s.compared;
        s.meanAbsoluteDifference += (std::abs(difference) - s.meanAbsoluteDifference) / s.compared;
        s.higher += difference > 0.5;
        s.lower += difference < -0.5;
        s.similar += std::abs(difference) <= 0.5;
      }
    }
    for (int value = int(Dimension::Status); value <= int(Dimension::Progress); ++value) {
      const auto d = static_cast<Dimension>(value);
      // Completion groups represent completed titles only.
      if ((d == Dimension::CompletionYear || d == Dimension::CompletionMonth) &&
          r.status != "Completed")
        continue;
      if (d == Dimension::ScoreDifference && (r.score <= 0 || r.communityScore <= 0)) continue;
      if (d == Dimension::Progress &&
          (r.total <= 0 || (!r.repeating && r.status != "Watching" && r.status != "Reading" &&
                            r.status != "On hold" && r.status != "Paused")))
        continue;
      for (const auto& key : keys(r, d)) {
        auto& g = groups[d][key];
        g.name = key;
        g.ids.append(r.id);
        g.units += units;
        g.seconds += seconds;
        if (r.score > 0) {
          ++g.scored;
          g.scoreSum += r.score;
        }
      }
    }
  }
  if (s.scored) {
    std::sort(scores.begin(), scores.end());
    s.median = (scores[(scores.size() - 1) / 2] + scores[scores.size() / 2]) / 2;
    s.deviation = std::sqrt(std::max(0.0, squares / s.scored));
    int most = 0;
    for (auto it = frequencies.begin(); it != frequencies.end(); ++it) {
      if (it.value() > most) {
        s.modes.clear();
        most = it.value();
      }
      if (it.value() == most) s.modes.append(it.key());
    }
  }
  s.weightedMean = s.ratedUnits ? s.weightedMean / s.ratedUnits : 0;
  for (auto it = groups.begin(); it != groups.end(); ++it) s.groups[it.key()] = it.value().values();
  return s;
}

ActivitySummary calculateActivity(const QList<ActivityChange>& changes, const QSet<int>& ids) {
  ActivitySummary result;
  QMap<QString, Group> days, months;
  for (const auto& change : changes) {
    if (!ids.contains(change.mediaId) || !change.time.isValid() || change.progress < 0 ||
        change.previousProgress < 0 || !std::isfinite(change.score) ||
        !std::isfinite(change.previousScore) || change.score < 0 || change.score > 10 ||
        change.previousScore < 0 || change.previousScore > 10)
      continue;
    if (change.progress == change.previousProgress && change.score == change.previousScore)
      continue;
    const auto date = change.time.toLocalTime().date();
    result.changes.append(change);
    if (!result.firstDate.isValid() || date < result.firstDate) result.firstDate = date;
    const auto added = std::max<qint64>(0, change.progress - change.previousProgress);
    result.addedUnits += added;
    result.scoreChanges += change.score != change.previousScore;
    if (!added) continue;
    const auto add = [&](QMap<QString, Group>& groups, const QString& key) {
      auto& group = groups[key];
      group.name = key;
      group.units += added;
      group.ids.append(change.mediaId);
    };
    add(days, date.toString(Qt::ISODate));
    add(months, date.toString("yyyy-MM"));
  }
  for (auto* groups : {&days, &months})
    for (auto& group : *groups) {
      const QSet<int> unique(group.ids.begin(), group.ids.end());
      group.ids = unique.values();
      std::sort(group.ids.begin(), group.ids.end());
    }
  result.days = days.values();
  result.months = months.values();
  std::stable_sort(result.changes.begin(), result.changes.end(), [](const auto& a, const auto& b) {
    return a.time != b.time ? a.time > b.time : a.id > b.id;
  });
  return result;
}

}  // namespace stats
