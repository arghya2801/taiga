# Taiga

[![](https://img.shields.io/github/license/erengy/taiga)](https://github.com/erengy/taiga/blob/master/LICENSE)
[![](https://img.shields.io/github/v/release/erengy/taiga)](https://taiga.moe/download.php)
[![](https://img.shields.io/discord/423475967051169813?logo=discord)](https://discord.gg/yeGNktZ)
[![](https://img.shields.io/github/sponsors/erengy?logo=github)](https://github.com/sponsors/erengy)

[Taiga](https://taiga.moe) is an open-source desktop application for Windows. It automatically detects the anime videos you watch on your computer and synchronizes your progress with [AniList](https://anilist.co), [Kitsu](https://kitsu.app) or [MyAnimeList](https://myanimelist.net). It helps you manage your anime library, discover new series, share watched episodes and download new ones.

## Manga tracking

Connect a MyAnimeList account in Settings, then open **Manga List** in the sidebar. The page loads your manga list from MyAnimeList and keeps a local copy for quick access on later launches. You can filter your list, add manga from the **Find manga** tab, edit reading status, chapter and volume progress, and score, or remove a title. Select a manga and use **+1 chapter** for a quick progress update. Manga progress changes only when you make an edit; video detection does not affect it.

## Local Windows build

With Git, Python, and Visual Studio 2022 C++ Build Tools installed, run this from PowerShell in the repository root:

```powershell
.\setup\build-local.ps1 -Run
```

The script fetches pinned submodules and Qt 6.10.2 when missing, builds Taiga, copies the required Qt runtime files, and launches the executable at `bin\RelWithDebInfo\Taiga.exe`. This portable build keeps its data in `bin\RelWithDebInfo\data`, separate from an installed Taiga release. Run the same command after changing the source to rebuild it.

To run an existing build from a visible PowerShell terminal without rebuilding, use `.\setup\run-local.ps1`.

## Links

- [Changelog](https://github.com/erengy/taiga/wiki/Changelog)
- [Contribution guidelines](https://github.com/erengy/taiga/wiki/Guidelines)
- [How to compile](https://github.com/erengy/taiga/wiki/How-to-Compile)

### Related projects

- [Anime relations](https://github.com/erengy/anime-relations) (episode redirections)
- [Anisthesia](https://github.com/erengy/anisthesia) (media detection library)
- [Anitomy](https://github.com/erengy/anitomy) (anime video filename parser)
- [taiga.moe](https://github.com/erengy/taiga-moe) (home page of Taiga)

## License

Taiga is licensed under [GNU General Public License v3](https://www.gnu.org/licenses/gpl-3.0.html).
