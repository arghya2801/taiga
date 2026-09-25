# Taiga (MyAnimeList client fork)

[![](https://img.shields.io/github/license/arghya2801/taiga)](LICENSE)

A Windows desktop client for keeping your [MyAnimeList](https://myanimelist.net) anime **and**
manga lists. It's a fork of [erengy/taiga](https://github.com/erengy/taiga)'s v2 rewrite. Upstream
Taiga is built around detecting the videos you play and updating your list automatically. This
fork keeps that working, but its focus is on being a fast, full-featured list client you use
directly: browse, sort, edit and look things up without opening the website.

AniList and Kitsu accounts still work for anime. Manga is MyAnimeList only.

## What's here

**Anime list**
- A fast list with sortable, show/hide columns (right-click any header). Layouts are remembered.
- An airing status column, progress bars that show aired and downloaded episodes, and ties
  sorted by title.
- Cards view with cached posters. Posters are stored small and compressed, so they load
  instantly after the first time.
- Configurable double-click and middle-click actions, and new-episode highlighting.

**Manga list (MyAnimeList)**
- Your full manga list, grouped by Reading, Completed, On hold, Dropped and Plan to read.
- Columns for chapters, volumes, score, type, average, publishing status, rereads, dates and
  last update.
- Details with cover, synopsis, authors, genres and publication info. Covers are cached.
- Edit status, chapters, volumes, score, rereading, priority, tags and comments, or remove the
  entry. **Read next chapter** does a quick +1.
- Search MyAnimeList from the search box and add new titles.

**Everything else**
- **Statistics:** totals, time watched and planned, mean score and a score chart.
- **Seasons:** one click to the previous, current or next season on the Search page.
- **Torrents:** RSS feeds and search, with v1's filter system (discard, select, prefer) and
  optional auto-download.
- **Sharing:** Discord Rich Presence and HTTP announce.
- **App:** a flat dark and light theme, tray options, start with Windows, update check, and
  profile and external links.
- **Detection:** automatic detection and updates from your media player are still available,
  and can be turned off.

`FEATURE_GAPS.md` tracks what's ported from Taiga v1.4.1 and what isn't.

## Getting started

1. Build and run it (see below).
2. In **Settings > Accounts**, select MyAnimeList and click **Authorize...**. Approve Taiga in
   your browser, then paste the code shown there into the prompt.
3. Press **Synchronize** in the toolbar to load your anime list. Open **Manga List** in the
   sidebar to load your manga.

Your lists are cached locally, so later launches open instantly and sync in the background.

## Building on Windows

With Git, Python, and Visual Studio 2022 C++ Build Tools installed, run this from PowerShell in
the repository root:

```powershell
.\setup\build-local.ps1 -Run
```

The script fetches the pinned submodules and Qt 6.10.2 when they're missing, builds Taiga, copies
the Qt runtime files, and launches `bin\RelWithDebInfo\Taiga.exe`. This portable build keeps its
data in `bin\RelWithDebInfo\data`, separate from any installed Taiga. Run the same command again
after changing the source.

To run an existing build without rebuilding, use `.\setup\run-local.ps1`.

`taiga-checks.exe`, built alongside the app, runs self-checks for the format strings and torrent
filters. It exits with a non-zero code on failure.

## Credits

Taiga is created by [Eren Okka](https://github.com/erengy). This fork builds on the v2 codebase
and its libraries:

- [Anime relations](https://github.com/erengy/anime-relations) (episode redirections)
- [Anisthesia](https://github.com/erengy/anisthesia) (media detection)
- [Anitomy](https://github.com/erengy/anitomy) (anime filename parser)

## License

Licensed under the [GNU General Public License v3](https://www.gnu.org/licenses/gpl-3.0.html),
like upstream Taiga.
