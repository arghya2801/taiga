# Taiga v1.4.1 vs this branch: feature status

v1.4.1 (`upstream/master`, Win32) against this branch (upstream `develop` + manga + the work
below). v1 source for reference is in `src/v1/`.

Status: **Done**, **Partial** (works, thinner than v1), **Skipped** (with the reason).

## Performance

| Problem | Fix |
|---|---|
| A sync emitted one model update per anime, each with a linear row lookup, and the sort proxy re-sorted every time (O(n²)) | `AnimeListModel` keeps an id→row hash and folds a burst of updates into one insert and one `dataChanged` |
| Every DB write opened SQLite, committed with an fsync, and closed it again. Search results were saved one row at a time. | The connection stays open in WAL mode, and search results are written in one batch |
| Posters were stored and decoded at full size, then scaled on every paint | Downscaled to 400 px and saved as JPEG q85 (about half the size); decoding and encoding happen off the UI thread |
| An uncached poster was read from disk (and failed) before it was fetched | Missing files go straight to the network |
| Manga covers had no cache and opened a new network manager each time | They use the same image cache as anime posters |
| The manga list re-sorted after every inserted row | Rows are inserted with sorting paused, then sorted once |
| Card paint cleaned the synopsis HTML on every frame | The cleaned synopsis is cached per anime |

## Anime list

| Feature | Status | Notes |
|---|---|---|
| Show/hide columns (right-click header), Reset headers | Done | Anime list, search, manga, torrents |
| Advanced list search | Done | v1 field queries, numeric comparisons, quoted phrases, genre/studio/note search; scores use 0-10 |
| Save column layout | Done | `session.json` → `*.headerState` |
| Airing status indicator | Done | Colored square in the title column, text in the tooltip |
| Secondary sort | Done | Ties sort by title (v1's default second key) |
| Progress bar extras | Done | Aired episodes shown faintly, available episodes as ticks |
| Highlight new episodes / show on top | Done | Anime List settings |
| Double-/middle-click actions | Done | Anime List settings |
| Manga columns | Done | Publishing, rereads, started, completed, last updated |

## Pages

| Feature | Status | Notes |
|---|---|---|
| Statistics | Done | Responsive chart dashboard: Overview, Ratings, Taste, Timeline and searchable Titles; MAL comparisons, sample cutoffs, completion dates, backlog/repeats, keyboard drill-down and CSV export |
| Torrents / RSS | Done | Source + search feeds, v1 filters and presets, filter editor, quick filters, archive, auto-check with notify/download, torrent app with `%file%`/`%folder%` |
| Seasons | Partial | One-click previous/current/next season on Search. No grouped view; sort by type instead. |
| Profile links | Done | Toolbar menu, per service |
| External links | Done | Editable in Application → Links |
| Home / Now Playing page | Skipped | Now Playing already shows as a bar; the Home page is still Eren's placeholder |

## Sharing

| Feature | Status | Notes |
|---|---|---|
| Discord Rich Presence | Done | Talks to Discord's local pipe directly, no library needed |
| HTTP announce | Done | With the v1 format language (`src/base/atf.cpp`) |
| Toggle sharing | Done | |
| mIRC, Twitter | Skipped | No users left / no free API |

## Application

| Feature | Status | Notes |
|---|---|---|
| Check for updates (menu + startup) | Done | GitHub releases |
| Scan available episodes (menu + startup) | Done | Walks library folders off the UI thread |
| Play next episode / random anime | Done | Uses scanned episodes |
| Start with Windows, start minimized | Done | |
| Minimize/close to tray | Done | |
| Hide sidebar | Done | Main menu |
| Tray notifications (recognized / not recognized) | Done | Fixed text rather than a custom format |
| Advanced → Cache | Done | Size and Clear |

## Recognition

| Feature | Status | Notes |
|---|---|---|
| Ask before updating | Done | Goes through the existing confirmation flow |
| Ignored strings | Done | Comma-separated, removed before parsing |
| Min file size, media player launch path, per-site streaming toggles | Skipped | Not requested often; add if needed |

## Checks

`build-local.ps1` builds and runs `taiga-checks`. It checks format strings, torrent filters,
account-statistics arithmetic and missing-data handling, advanced search, and manga serialization/export.

`taiga-integration-checks` uses temporary SQLite/cache files and a loopback HTTP server. It exercises
shared-connection persistence, offline manga queue recovery, failed-request retries, edits made
while a request is running, actual list filtering, and statistics filters/drill-downs. Its test
build routes all HTTP to loopback and cannot read the portable application's account data.
Both suites are registered with CTest.

## Bugs fixed on this branch

- **MAL anime list saved only 1 entry.** `anime_list` was keyed on the list-entry id, which MAL
  doesn't provide. Fixed with an in-place migration in `createAnimeList.sql` and `anime_db.cpp`.

## Account statistics and performance work, October 9, 2026

Recognition is maintenance-only. Priorities are account analysis, responsive lists and practical v1 workflows.

- Anime and MAL manga use the same statistics engine. Scores normalize to 0-10 and unrated
  entries are excluded from averages. Mean, median, tied modes, population deviation and
  personal-minus-community comparisons use explicit sample counts.
- Breakdowns include status, format, genre, studio/author, release year/season, completion
  year/month and score. Filters intersect; selecting a group displays its contributing titles.
  Groups can rank by count, mean score, progress or watch time, with a minimum rated sample.
  Dedicated chart pages replace the permanent tables; timeline charts are chronological and
  include empty months. Score gaps, length buckets and active progress have title drilldowns.
- Watch time includes known rewatch totals and identifies estimated durations. Unknown totals
  remain unknown; backlog totals include only known remaining episodes/chapters. Manga does
  not fabricate reading hours. Completion charts use personal list dates and report missing dates.
- Daily episode history and historical score changes are not inferred from a current list snapshot.
- Statistics aggregate on a worker and refresh after list changes. Returning to an unchanged
  page reuses the result. Image-cache size scans and manga cache reads/writes run off the UI thread.
- Manga edits update a single displayed row and preserve selection. A per-account, atomic pending
  file survives restart, retries failed requests, and keeps newer edits when older responses arrive.
  Manga reading dates can be edited; XML and Markdown exports are available from Manga List.
- Sort comparisons reuse decoded titles and avoid repeated model/database lookups. Cached
  titles invalidate when metadata or the selected title language changes.
- Synchronization leaves the app usable. Fetched list entries preserve queued edits and newer
  local timestamps. Manga refreshes cannot overlap or overwrite a newer confirmed mutation.
- History and the anime queue no longer close the shared database connection. This fixes a
  regression where later list writes silently returned because the connection was closed.

### Broader v1 workflow audit

These are source-level comparisons with `src/v1`, except where the integration suite verifies
behavior. “Present” here means implemented, not that every live-service workflow was tested.

| Workflow | Current result | Evidence / remaining difference |
|---|---|---|
| Field-based list search | Restored | `media/anime_query.*`; id, eps, title, genre, producer/studio, tag, note, type, season, year, rewatch, duration, score, average, status. Internal v1 debug queries omitted. |
| Batch episode, score, status, notes and date actions | Present | `gui/media/media_menu.cpp`; multi-selection, date clearing and dates from airing/last update. |
| Selection and keyboard actions | Partial | Extended selection, Ctrl+A, configurable mouse actions, invert selection and menu Delete are present. v1 Enter follows the double-click action; current Enter opens details. Delete outside the menu and v1 Ctrl+C, Ctrl+plus/minus, Ctrl+digits, Ctrl+O/N/R shortcuts are missing. Source comparison: v1 `dlg_anime_list.cpp` lines 851-935 versus current list view/base. |
| List columns, header persistence and cards | Present | `gui/common/anime_list_view*`; secondary title sorting and cached posters. |
| Grouped results by list status | Still missing | v1 shows status groups when searching across the list; current list is flat with sidebar status filtering. |
| Season browsing | Partial | Search has season/year/type/status filters and previous/current/next shortcuts; v1's grouped season layout is absent. |
| Export | Present, service limits | Anime XML/Markdown and MAL manga XML/Markdown. Cross-service MAL-ID mapping remains incomplete. |
| Import / migration | Partial | Existing v1 anime/list/history migration; sharing/RSS settings and service-ID mapping remain incomplete. Neither version's export should be described as an in-app import workflow. |
| Torrents and sharing | Present with omissions | Feed rules/presets/editor/archive/auto-download, Discord and HTTP announce. Legacy mIRC/Twitter stay low priority. |
| v1 statistics | Expanded for account analysis | Account metrics exceed v1's totals and score histogram. v1's torrent-file counts and connection counters remain diagnostic omissions; cache size and uptime are retained. |
| Manga | Added beyond v1 | Manual MAL tracking, metadata cache, dates, export, statistics and durable retries. |

The grouped list/season layouts and migration gaps remain explicit follow-up features; this
work completes the account-statistics milestone and its performance/persistence checks.

Local large-list measurements and reproduction commands are in [docs/performance.md](docs/performance.md).
