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

#pragma once

#include <QString>

#include "taiga/settings.hpp"

// Simple options: a key and its default in one place, so callers and settings pages can't
// disagree on the default.
namespace taiga::opt {

struct Bool {
  const char* key;
  bool fallback;
  bool get() const {
    return settings.option(QLatin1StringView{key}, fallback).toBool();
  }
  void set(bool value) const {
    settings.setOption(QLatin1StringView{key}, value);
  }
};

struct Int {
  const char* key;
  int fallback;
  int get() const {
    return settings.option(QLatin1StringView{key}, fallback).toInt();
  }
  void set(int value) const {
    settings.setOption(QLatin1StringView{key}, value);
  }
};

struct Str {
  const char* key;
  const char* fallback;
  QString get() const {
    return settings.option(QLatin1StringView{key}, QString::fromUtf8(fallback)).toString();
  }
  void set(const QString& value) const {
    settings.setOption(QLatin1StringView{key}, value);
  }
};

// Anime list
enum class ListAction { ViewDetails, EditEntry, PlayNextEpisode, OpenFolder, OpenPage, Nothing };
inline constexpr Int listDoubleClick{"animeList.doubleClickAction",
                                     static_cast<int>(ListAction::ViewDetails)};
inline constexpr Int listMiddleClick{"animeList.middleClickAction",
                                     static_cast<int>(ListAction::PlayNextEpisode)};
inline constexpr Bool highlightNewEpisodes{"animeList.highlightNewEpisodes", true};
inline constexpr Bool newEpisodesOnTop{"animeList.newEpisodesOnTop", false};

// Application
inline constexpr Bool checkUpdatesOnStartup{"app.checkUpdatesOnStartup", true};
inline constexpr Bool scanOnStartup{"library.scanOnStartup", true};
inline constexpr Bool startMinimized{"app.startMinimized", false};
inline constexpr Bool minimizeToTray{"app.minimizeToTray", false};
inline constexpr Bool closeToTray{"app.closeToTray", false};
inline constexpr Bool showSidebar{"app.showSidebar", true};
// One "Name|URL" per line, "-" for a separator. v1's defaults.
inline constexpr Str externalLinks{"app.externalLinks",
                                   "MALgraph|https://anime.plus/\n"
                                   "-\n"
                                   "AniChart|https://anichart.net/airing\n"
                                   "Monthly.moe|https://www.monthly.moe/weekly\n"
                                   "-\n"
                                   "Anime Scene Search Engine|https://trace.moe/"};

// Sharing
inline constexpr Bool sharingEnabled{"share.enabled", true};
inline constexpr Bool discordEnabled{"share.discord.enabled", false};
inline constexpr Bool discordShowGroup{"share.discord.showGroup", true};
inline constexpr Bool discordShowTime{"share.discord.showTime", true};
inline constexpr Bool discordShowUsername{"share.discord.showUsername", true};
inline constexpr Str discordApplicationId{"share.discord.applicationId", "379871385176244224"};
inline constexpr Bool httpEnabled{"share.http.enabled", false};
inline constexpr Str httpUrl{"share.http.url", ""};
inline constexpr Str httpFormat{"share.http.format",
                                "user=%user%&name=%title%&ep=%episode%"
                                "&eptotal=$if(%total%,%total%,?)&score=%score%"
                                "&picurl=%image%&playstatus=%playstatus%"};

// Torrents
inline constexpr Str torrentSource{"torrent.source", "https://nyaa.si/?page=rss&c=1_2&f=0"};
inline constexpr Str torrentSearchSource{"torrent.searchSource",
                                         "https://nyaa.si/?page=rss&c=1_2&f=0&q=%title%"};
inline constexpr Bool torrentFiltersEnabled{"torrent.filters.enabled", true};
inline constexpr Str torrentFilters{"torrent.filters.list", ""};  // JSON; empty means defaults
inline constexpr Bool torrentAutoCheck{"torrent.autoCheck.enabled", false};
inline constexpr Int torrentAutoCheckMinutes{"torrent.autoCheck.minutes", 60};
enum class TorrentNewAction { Notify, Download };
inline constexpr Int torrentNewAction{"torrent.autoCheck.action",
                                      static_cast<int>(TorrentNewAction::Notify)};
inline constexpr Str torrentApp{"torrent.app.path", ""};
inline constexpr Str torrentAppArgs{"torrent.app.arguments", "\"%file%\""};

// Recognition
inline constexpr Bool notifyRecognized{"track.notify.recognized", true};
inline constexpr Bool notifyNotRecognized{"track.notify.notRecognized", false};
inline constexpr Bool askBeforeUpdating{"track.update.ask", false};
inline constexpr Str ignoredStrings{"recognition.ignoredStrings", ""};

}  // namespace taiga::opt
