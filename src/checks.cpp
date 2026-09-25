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

#include <cstdio>

#include "base/atf.hpp"
#include "base/string.hpp"
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
      {u"title"_s, u"Frieren (TV)"_s}, {u"episode"_s, u"7"_s}, {u"total"_s, u"28"_s},
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

  Filter resolution{.name = u"1080p only"_s,
                    .action = FilterAction::Discard,
                    .match = FilterMatch::Any,
                    .conditions = {{FilterElement::Resolution, FilterOperator::NotEquals,
                                    u"1080p"_s}}};
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

}  // namespace

int main() {
  checkAtf();
  checkFeedFilters();
  std::printf(failures ? "%d check(s) failed.\n" : "All checks passed.\n", failures);
  return failures;
}
