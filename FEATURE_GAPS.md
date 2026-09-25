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
| Statistics | Done | List totals, time watched/planned, mean/deviation, score chart, local data, uptime |
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

`taiga-checks` (built with the solution) runs asserts on the format language and feed filters.
It exits non-zero on failure.

## Bugs fixed on this branch

- **MAL anime list saved only 1 entry.** `anime_list` was keyed on the list-entry id, which MAL
  doesn't provide. Fixed with an in-place migration in `createAnimeList.sql` and `anime_db.cpp`.
