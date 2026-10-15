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

// Self-checks for logic that's easy to break and hard to see break in the UI.
// Build and run the `taiga-checks` target; a non-zero exit code means a failure.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <cmath>
#include <cstdio>

#include "base/atf.hpp"
#include "base/string.hpp"
#include "media/account_stats.hpp"
#include "media/anime_query.hpp"
#include "media/manga_library.hpp"
#include "track/feed_filter.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
  }
}

void checkAtf() {
  const atf::Fields fields{
      {u"title"_s, u"Frieren (TV)"_s},
      {u"episode"_s, u"7"_s},
      {u"total"_s, u"28"_s},
      {u"group"_s, {}},
  };
  const auto format = [&](const char* str) { return atf::replace(QString::fromUtf8(str), fields); };

  check(format("%title%") == u"Frieren (TV)", "variables keep special characters");
  check(format("$if(%episode%,Episode %episode%$if(%total%,/%total%))") == u"Episode 7/28",
        "nested if");
  check(format("$if(%group%,by %group%,no group)") == u"no group", "empty variable is false");
  check(format("$if($greater(%episode%,10),late,early)") == u"early", "numeric comparison");
  check(format("$num(%episode%,3)") == u"007", "num pads with zeros");
  check(format("$upper(%title%)") == u"FRIEREN (TV)", "upper");
  check(format("100\\% done") == u"100% done", "escaped percent");
  check(format("%unknown%") == u"%unknown%", "unknown variables are kept");
  check(format("a  b\\n\\nc") == u"a b\nc", "whitespace is collapsed");
}

void checkFeedFilters() {
  using namespace track::feed;

  Item item;
  item.title = u"[Group] Frieren - 07 [1080p].mkv"_s;
  item.animeId = 1;
  item.episode = 7;
  item.resolution = u"1080p"_s;
  item.group = u"Group"_s;

  Filter resolution{
      .name = u"1080p only"_s,
      .action = FilterAction::Discard,
      .match = FilterMatch::Any,
      .conditions = {{FilterElement::Resolution, FilterOperator::NotEquals, u"1080p"_s}}};
  check(!resolution.matches(item, {}), "a matching resolution isn't discarded");
  item.resolution = u"720p"_s;
  check(resolution.matches(item, {}), "another resolution is discarded");

  Filter watched{.name = u"New episodes"_s,
                 .action = FilterAction::Discard,
                 .match = FilterMatch::All,
                 .conditions = {{FilterElement::EpisodeNumber, FilterOperator::LessThanOrEquals,
                                 u"%watched%"_s}}};
  check(watched.matches(item, {.watched = 7}), "watched episodes are discarded");
  check(!watched.matches(item, {.watched = 6}), "the next episode is kept");
}

void checkStatistics() {
  stats::Record a;
  a.id = 1;
  a.title = "One";
  a.status = "Completed";
  a.format = "TV";
  a.score = 8;
  a.communityScore = 7;
  a.progress = a.total = 12;
  a.repeats = 2;
  a.minutesPerEpisode = 24;
  a.genres = {"Action", "Drama", "Drama"};
  a.studios = {"Studio"};
  a.releaseYear = "2024";
  a.completionYear = "2025";
  a.completionMonth = "2025-06";
  auto b = a;
  b.id = 2;
  b.score = 6;
  b.communityScore = 0;
  b.repeats = 0;
  b.status = "Watching";
  b.progress = 4;
  b.completionYear.clear();
  b.completionMonth.clear();
  b.updated = QDate(2024, 1, 1);
  auto c = b;
  c.id = 3;
  c.score = 0;
  c.total = 0;
  c.repeats = 1;
  c.minutesPerEpisode = 0;
  auto s = stats::calculate({a, b, c, a}, {}, QDate(2026, 1, 1));
  check(s.records.size() == 3, "statistics deduplicate account IDs");
  check(s.units == 44 && s.repeatUnits == 24, "rewatches count known totals once per repeat");
  check(s.remainingUnits == 8 && s.remainingSeconds == 8 * 24 * 60,
        "backlog excludes completed titles and unknown totals");
  check(s.scored == 2 && s.mean == 7 && s.median == 7 && s.deviation == 1,
        "unrated titles do not dilute scores");
  check(s.modes == QList<double>{6, 8}, "all tied modes are returned");
  check(s.compared == 1 && s.meanDifference == 1,
        "score comparison excludes missing community scores");
  check(s.communityMean == 7 && s.meanAbsoluteDifference == 1 && s.higher == 1 && s.lower == 0 &&
            s.similar == 0,
        "MAL comparison uses matched rated titles only");
  check(s.ratedUnits == 40 && std::abs(s.weightedMean - 7.8) < 0.0001,
        "episode-weighted score includes known rewatches and excludes unrated progress");
  check(s.groups[stats::Dimension::ScoreDifference].size() == 1 &&
            s.groups[stats::Dimension::ScoreDifference][0].ids == QList<int>{1},
        "score-gap chart buckets retain the actual comparable title IDs");
  check(s.groups[stats::Dimension::Progress].size() == 1 &&
            s.groups[stats::Dimension::Progress][0].name == "1" &&
            s.groups[stats::Dimension::Progress][0].ids == QList<int>{2},
        "progress chart excludes finished titles and unknown totals");
  check(s.groups[stats::Dimension::Length][0].ids == QList<int>({1, 2}),
        "series-length chart groups by known total rather than current progress");
  check(s.unknownTotals == 1 && s.unknownDuration == 1 && s.stale == 2,
        "unknown metadata and stale backlog are counted");
  check(s.groups[stats::Dimension::Genre][1].ids.size() == 3,
        "duplicate genres do not inflate sample sizes");
  check(s.groups[stats::Dimension::CompletionMonth].size() == 1 &&
            s.groups[stats::Dimension::CompletionMonth][0].ids == QList<int>{1},
        "completion timeline uses finished titles only");
  stats::Filter f;
  f.status = "Completed";
  f.genre = "Drama";
  f.year = "2024";
  check(stats::calculate({a, b, c}, f).records.size() == 1, "statistics filters intersect");
  f.studio = "Missing";
  check(stats::calculate({a, b, c}, f).records.isEmpty(), "empty filtered statistics are safe");
  auto odd = a;
  odd.id = 4;
  odd.score = NAN;
  odd.communityScore = INFINITY;
  odd.total = -1;
  odd.progress = -5;
  odd.repeats = -1;
  s = stats::calculate({odd});
  check(s.scored == 0 && s.units == 0 && s.unknownTotals == 1,
        "invalid values cannot corrupt statistics");
  check(s.groups[stats::Dimension::ScoreDifference].isEmpty() && s.weightedMean == 0,
        "invalid scores cannot enter comparison or weighted averages");
  auto low = a;
  low.id = 5;
  low.score = 1;
  low.communityScore = 10;
  check(stats::calculate({low}).groups[stats::Dimension::ScoreDifference][0].name == "-4",
        "extreme score gaps fit the bounded comparison histogram");
  auto large = a;
  large.total = large.progress = 1000000;
  large.repeats = 1000;
  check(stats::calculate({large}).seconds > 1000000000000LL, "watch time uses 64-bit arithmetic");
  large.status = "Watching";
  large.total = 1;
  large.progress = 1LL << 40;
  check(stats::calculate({large}).groups[stats::Dimension::Progress][0].name == "3",
        "oversized progress clamps before converting chart bucket to integer");
  auto active = a;
  active.repeats = 0;
  active.repeating = true;
  active.progress = 11;
  s = stats::calculate({active});
  check(s.units == 23 && s.repeatUnits == 11 && s.activeRepeats == 1 && s.repeatedTitles == 1,
        "an active first repeat preserves the original completed pass");
  check(s.remainingUnits == 1 && s.groups[stats::Dimension::Progress].size() == 1,
        "active repeats count toward backlog even with completed status");
  active.repeating = false;
  active.repeats = 1;
  active.progress = 12;
  s = stats::calculate({active});
  check(s.units == 24 && s.repeatUnits == 12 && s.activeRepeats == 0,
        "finishing a repeat adds only the last episode");
  active.repeating = true;
  active.progress = 5;
  s = stats::calculate({active});
  check(s.units == 29 && s.repeatUnits == 17,
        "another active repeat includes earlier completed repeats");
  active.total = 0;
  s = stats::calculate({active});
  check(s.units == 5 && s.repeatUnits == 5 && s.remainingUnits == 0,
        "unknown totals retain observed repeat progress without inventing earlier passes");
}

void checkActivity() {
  const QDateTime time(QDate(2026, 10, 15), QTime(12, 0));
  stats::ActivityChange a{1, 1, time, 2, 5, 7, 8};
  auto correction = a;
  correction.id = 2;
  correction.previousProgress = 5;
  correction.progress = 3;
  correction.previousScore = correction.score = 8;
  auto other = a;
  other.id = 3;
  other.mediaId = 2;
  auto earlier = a;
  earlier.id = 4;
  earlier.time = time.addMonths(-1);
  earlier.previousProgress = 0;
  earlier.progress = 1;
  earlier.previousScore = earlier.score = 0;
  auto invalid = a;
  invalid.id = 5;
  invalid.score = NAN;
  auto unchanged = a;
  unchanged.id = 6;
  unchanged.previousProgress = unchanged.progress;
  unchanged.previousScore = unchanged.score;
  const auto result =
      stats::calculateActivity({a, correction, other, earlier, invalid, unchanged}, {1});
  check(result.changes.size() == 3 && result.scoreChanges == 1 && result.addedUnits == 4,
        "local history filters titles and excludes corrections from newly added units");
  check(result.days.size() == 2 && result.months.size() == 2 &&
            result.firstDate == time.addMonths(-1).date(),
        "activity groups use recorded local dates without inventing earlier activity");
  check(result.days.last().ids == QList<int>{1} && result.days.last().units == 3,
        "daily chart retains distinct contributing title IDs");
  check(result.changes[0].id == 2 && result.changes.last().id == 4,
        "activity history sorts by time then insertion order");
  check(stats::calculateActivity({a}, {}).changes.isEmpty(),
        "empty activity selection stays empty");
}

void checkMangaIo() {
  manga::Entry e;
  e.id = 42;
  e.title = "A & B <C>";
  e.status = "reading";
  e.chaptersRead = 12;
  e.volumesRead = 2;
  e.startedReading = "2025-00-00";
  e.genres = {"Drama"};
  e.authors = {"Author"};
  e.tags = {"tag"};
  e.comments = "Quotes \" and & symbols";
  e.onList = true;
  const auto restored = manga::fromJson(manga::toJson(e));
  auto unknown = e;
  unknown.startedReading = "0000-00-00";
  check(manga::fromJson(manga::toJson(unknown)).startedReading.isEmpty(),
        "unknown manga dates normalize to empty fields");
  check(restored.id == e.id && restored.genres == e.genres && restored.authors == e.authors &&
            restored.startedReading == e.startedReading && restored.comments == e.comments,
        "manga cache preserves statistics metadata and partial dates");
  QTemporaryDir dir;
  check(manga::exportList({e}, dir.filePath("manga.xml"), true), "manga XML export succeeds");
  QFile file(dir.filePath("manga.xml"));
  check(file.open(QIODevice::ReadOnly), "manga XML can be read back");
  QXmlStreamReader xml(file.readAll());
  QString title, id;
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isStartElement() && xml.name() == QStringLiteral("manga_title"))
      title = xml.readElementText();
    if (xml.isStartElement() && xml.name() == QStringLiteral("manga_mangadb_id"))
      id = xml.readElementText();
  }
  check(!xml.hasError() && title == e.title && id == "42",
        "manga XML escapes text and preserves MAL IDs");
  check(!manga::exportList({e}, dir.filePath("missing/list.xml"), true),
        "export reports file-open failures");
}

void checkQueries() {
  anime::Details a;
  a.id = 7;
  a.type = anime::Type::Tv;
  a.episode_count = 12;
  a.titles.romaji = "A Long Title";
  a.genres = {"Drama", "Action"};
  a.studios = {"Studio A"};
  a.date_started = FuzzyDate("2024-04-01");
  a.score = 7.5;
  ListEntry e;
  e.score = 80;
  e.status = anime::list::Status::Watching;
  e.notes = "Try again";
  check(anime::Query("genre:drama score:>=8 year:2024 type:tv").matches(a, &e),
        "v1-style query intersects typed fields");
  check(anime::Query("title:\"Long Title\" studio:\"Studio A\"").matches(a, &e),
        "query supports quoted phrases");
  check(anime::Query("Drama note:again season:spring eps:<=12 status:watching").matches(a, &e),
        "query searches genres, notes, seasons and status");
  check(!anime::Query("genre:!=drama").matches(a, &e), "query negation excludes matching fields");
  check(!anime::Query("score:abc").matches(a, &e), "invalid numeric queries match nothing");
  check(!anime::Query("note:again").matches(a, nullptr), "query handles entries outside the list");
}

void benchmarkStatistics() {
  for (const int count : {1000, 10000, 50000}) {
    QList<stats::Record> records;
    records.reserve(count);
    for (int i = 1; i <= count; ++i) {
      stats::Record r;
      r.id = i;
      r.title = QString::number(i);
      r.score = 1 + i % 10;
      r.status = i % 3 ? "Completed" : "Watching";
      r.format = "TV";
      r.genres = {"Action", "Drama"};
      r.studios = {QString::number(i % 80)};
      r.releaseYear = QString::number(1980 + i % 46);
      r.progress = 6;
      r.total = 12;
      r.minutesPerEpisode = 24;
      records.append(r);
    }
    QElapsedTimer clock;
    clock.start();
    const auto s = stats::calculate(records);
    check(s.records.size() == count && s.units == qint64(count) * 6,
          "large-list statistics preserve all entries");
    std::printf("Statistics: %d titles, %.2f ms\n", count, clock.nsecsElapsed() / 1000000.0);
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  checkAtf();
  checkFeedFilters();
  checkStatistics();
  checkActivity();
  checkMangaIo();
  checkQueries();
  if (app.arguments().contains("--benchmark")) benchmarkStatistics();
  std::printf(failures ? "%d check(s) failed.\n" : "All checks passed.\n", failures);
  return failures;
}
