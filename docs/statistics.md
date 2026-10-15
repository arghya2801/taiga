# MyAnimeList statistics

Open **Statistics** from the sidebar. The page uses the account list already saved by Taiga;
**Sync list** downloads updates. Calculations run in the background and old filter results
cannot replace a newer selection.

## Explore your list

- **Overview:** list status, personal score distribution, genres, formats, unfinished titles
  and recorded rewatches. The totals above the tabs show titles, episodes, estimated watch
  time and mean score. The backlog includes planned, watching and on-hold titles with known
  totals; the stalled-title button finds unfinished entries unchanged for at least 180 days.
- **Ratings:** mean, median, tied modes, population standard deviation and episode-weighted
  mean; a score histogram; your score minus MAL's mean; and the largest disagreements.
  Comparisons and both matched-title averages use only titles with both scores available.
- **Taste:** rank genres, studios, formats, release years/seasons and series lengths by title
  count, personal mean, episodes or estimated watch time. Mean rankings require five rated
  titles by default; change the minimum to adjust the sample cutoff. Show the top 15 or all
  groups. Counts include every title in the current selection, including planned titles.
- **Timeline:** completions by year and month, release years, series lengths and progress
  toward finishing currently watching/on-hold titles. Select the completion year to see all
  twelve months, including months with no completions.
- **Titles:** a searchable, sortable table of the titles contributing to a chart. Open a
  title with Enter or a double-click. Export the displayed selection as CSV.

Click any bar to open its contributing titles. Arrow keys navigate chart items, and Enter
or Space opens them. **Back to chart** or Escape returns to the originating page. Ctrl+1
through Ctrl+5 selects a page. Each chart item also exposes its complete label, value and
description to native accessibility tools.

**Filters** reveals status, format, release year, genre and studio controls. Filters intersect
and apply across every page. **All filtered titles** clears a chart drilldown without removing
the global filters. Cards reflow into a single column at narrower window widths.

The Anime/Manga selector uses the shared MAL manga cache for manga. Chapters, volumes,
authors and rereads replace the corresponding anime metrics. Reading hours are not estimated.

## What the numbers mean

Unrated titles never count as zero in score averages. The episode-weighted mean weights
each rated title by its recorded watched episodes, including repeats whose totals are known.
Multi-genre or multi-studio titles contribute to each group, so group totals can overlap.

Watch time is recorded episodes multiplied by episode duration, plus recorded rewatch counts
multiplied by known episode totals. Missing durations may use Taiga's duration estimate.
Unknown totals remain unknown, and cannot add fabricated rewatch episodes or backlog units.
**About these numbers** shows the current selection's coverage.

Completion charts use your personal finish dates. A year-only date contributes to the yearly
chart but not the monthly chart. Missing dates are counted explicitly and excluded from the
corresponding chart. Daily episode activity and earlier score changes cannot be reconstructed
from a current list snapshot.

## Design references

The redesign borrows separate analysis sections and count/score/time rankings from
[AniList's statistics overview](https://anilist.co/user/Rude/stats/anime/overview) and
[genre statistics](https://docs.anilist.co/reference/object/usergenrestatistic), plus dedicated
[ratings](https://anime.plus/Deividas/ratings,anime),
[completion history](https://anime.plus/Deividas/history,anime) and
[taste rankings](https://anime.plus/Deividas/favorites,anime) from anime.plus.
It uses MAL data already fetched by Taiga and adds no charting dependency.
