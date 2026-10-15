# Account statistics and list performance

Measured locally on October 9, 2026, with MSVC 2022, Qt 6.10.2, Windows x64 and
RelWithDebInfo. These are synthetic account lists with no network latency or poster loading.
The integration checks use a temporary SQLite database and a loopback HTTP server.

| Measurement | Size | Result |
|---|---:|---:|
| Statistics aggregation, worker thread | 1,000 titles | 27 ms |
| Statistics aggregation, worker thread | 10,000 titles | 247 ms |
| Statistics aggregation, worker thread | 50,000 titles | 1,201 ms |
| SQLite batch, metadata and list entries | 10,000 titles | 443 ms |
| Anime score sort, with title ties, before comparison optimization | 10,000 titles | 735 ms |
| Same sort after direct model access and title caching | 10,000 titles | 196 ms |
| Field query, `genre:drama score:>=8` | 10,000 titles | 35 ms |
| Statistics scrolling plus full-widget rendering | 10,000 titles | 13.6 ms/frame |
| Peak process working set, without posters | 10,000 titles | 93.8 MiB |
| Qt and empty-cache initialization in the test harness | Empty | 124 ms |

The redesigned dashboard was checked again with a varied 10,000-title fixture on the same
machine. Overview rendering took 7.15 ms/frame, sorting 198 ms and filtering 34 ms. Peak
working set before opening the main-window checks was 102.6 MiB. Aggregation with the added
score-gap, series-length and progress groups took 31 ms for 1,000 titles, 300 ms for 10,000
and 1,578 ms for 50,000. The rendering figures describe different page contents, so they
are not a controlled before/after comparison. Aggregation remains off the UI thread.

## Whole-application checks, October 15, 2026

The expanded benchmark runs the real `Application::run()` startup path in separate processes
against a temporary 10,000-title SQLite account. It renders the first main-window frame,
loads generated cached JPEG covers through the real image provider, scrolls the card view,
and delivers ten 100-title metadata pages while a UI timer measures event-loop delays.

| Measurement | Result |
|---|---:|
| Fresh process launch to first main-window frame, first run | 1,814 ms |
| Fresh process launch to first main-window frame, repeated run | 1,479 ms |
| Decode 24 cached covers into the pixmap cache | 32 ms |
| Full main-window card scrolling and rendering | 27.0 ms/frame |
| Largest UI heartbeat gap during the ten metadata pages | 28 ms |

These are fresh-process starts with the operating system's filesystem cache retained, not
cold-boot disk measurements. Network access, recognition and startup folder scanning are
disabled in the probe. Cover measurements use synthetic local images, not remote downloads.
The test executable routes all HTTP to loopback and the startup probe cannot attach to or
activate a user's running Taiga instance. The extra probe path is absent from production.

These are single local measurements, not latency guarantees. The earlier empty-cache harness
initialization excludes process loading before `main()`; the fresh-process figures above include it.
Rendering uses the Windows font engine with test widgets kept hidden. The query and sort
measurements use the actual anime list model and proxy, rather than a stand-in container.

Run from the repository root:

```powershell
.\setup\build-local.ps1
.\setup\check-local.ps1 -Benchmark -Screenshot
```

The screenshot checks write `statistics-dark-0.png` through `statistics-dark-4.png`, their light
counterparts, `statistics-narrow.png` and `statistics-backlog.png` beside the executables in
`bin/RelWithDebInfo`. `statistics-main-window.png` also covers the actual sidebar and page
styling. Fixtures include varied statuses, scores, dates, formats and studios.
`grouped-list-check.png` covers the grouped layout and its existing columns.
`statistics-taste-narrow.png` checks the completion-period comparison at a narrower width.

## What changed

- Sort comparisons get anime/list entries directly from the model and reuse decoded titles.
  Metadata changes invalidate the relevant title; changing title language resets the cache.
- Field queries parse once when the text changes. Filtering avoids constructing QVariant
  wrappers for every row's anime and list entry.
- Statistics calculate in a worker, debounce sync bursts, discard obsolete results, and reuse
  results when an unchanged page reopens. Title drill-down uses a virtual table model.
- Manga edits update one row. Cache loading, serialization and full cache writes use a worker;
  the small pending-change file commits atomically before accepting an edit.
- Image-cache directory scans run in a worker. A scan started before Clear cannot publish an
  outdated size after the cache generation changes.
- History and the anime queue keep the shared SQLite connection open. Regression checks
  verify that later list edits actually reach SQLite and update the in-memory list.
- Synchronization disables its toolbar action while leaving the rest of the window usable.
  Incoming list entries preserve queued edits and newer local timestamps.

## Validation

The logic suite covers score arithmetic, tied modes, population deviation, unrated and missing
metadata, repeat totals, 64-bit watch times, intersecting filters, completion groups, field
queries, JSON round trips and XML escaping/failures.

The integration suite covers the real database, queued offline manga edits, restart recovery,
HTTP failure/retry, edits made during a request, response timestamps and reading dates, title
cache invalidation, statistics filtering/drill-down, manga row updates and usable background sync.
The HTTP transport in the integration binary is compiled to redirect requests to loopback;
it cannot send mutations to a live service. Production builds use the normal transport.

The UI checks also cover saved-view persistence and restoration, period sample cutoffs and
drilldowns, separate page searches, Back/Forward branching, and 30 selected-row view transitions
per run. View-switch checks preserve both selected titles and the viewport anchor.

Restored grouped list/season layouts and remaining v1 migration limits are documented in
`FEATURE_GAPS.md`.
