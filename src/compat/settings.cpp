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

#include "settings.hpp"

#include <QJsonDocument>
#include <QMap>
#include <QNetworkProxy>
#include <QUrl>
#include <QXmlStreamReader>
#include <algorithm>
#include <chrono>

#include "base/log.hpp"
#include "base/string.hpp"
#include "base/xml.hpp"
#include "compat/common.hpp"
#include "media/anime.hpp"
#include "taiga/accounts.hpp"
#include "taiga/options.hpp"
#include "taiga/settings.hpp"
#include "track/feed_filter.hpp"

#define XML_ATTR(name) xml.attributes().value(name)
#define XML_ATTR_BOOL(name) (XML_ATTR(name) == u"true" || XML_ATTR(name) == u"1")
#define XML_ATTR_INT(name) XML_ATTR(name).toInt()
#define XML_ATTR_STR(name) XML_ATTR(name).toString().toStdString()

namespace compat::v1 {

void parseAccountElement(QXmlStreamReader&, const taiga::Settings&, const taiga::Accounts&);
void parseAnimeElement(QXmlStreamReader&, const taiga::Settings&);
void parseAnimeItemsElement(QXmlStreamReader&, QList<anime::Settings>&);
void parseProgramElement(QXmlStreamReader&, const taiga::Settings&, QMap<QString, QVariant>&, bool);
void parseRecognitionElement(QXmlStreamReader&, const taiga::Settings&);
void parseAnnounceElement(QXmlStreamReader&, QMap<QString, QVariant>&);
void parseRssElement(QXmlStreamReader&, QMap<QString, QVariant>&);

bool readSettings(const std::string& path, const taiga::Settings& settings,
                  const taiga::Accounts& accounts, bool onlyMissingOptions) {
  // Validate first so a truncated legacy file cannot partially overwrite settings.
  {
    base::XmlFileReader check;
    if (!check.open(QString::fromStdString(path), removeInvalidCharacterReferences)) return false;
    if (!check.readElement(u"settings")) return false;
    while (!check.atEnd()) check.readNext();
    if (check.hasError()) {
      qWarning() << "Invalid v1 settings:" << check.errorString();
      return false;
    }
  }
  base::XmlFileReader xml;

  if (!xml.open(QString::fromStdString(path), removeInvalidCharacterReferences)) {
    qCritical() << xml.file().errorString();
    return false;
  }

  if (!xml.readElement(u"settings")) {
    xml.raiseError("Invalid settings file.");
  }

  QMap<QString, QVariant> options;
  while (xml.readNextStartElement()) {
    if (xml.name() == u"account" && !onlyMissingOptions) {
      parseAccountElement(xml, settings, accounts);
    } else if (xml.name() == u"anime" && !onlyMissingOptions) {
      parseAnimeElement(xml, settings);
    } else if (xml.name() == u"recognition" && !onlyMissingOptions) {
      parseRecognitionElement(xml, settings);
    } else if (xml.name() == u"program") {
      parseProgramElement(xml, settings, options, onlyMissingOptions);
    } else if (xml.name() == u"announce") {
      parseAnnounceElement(xml, options);
    } else if (xml.name() == u"rss") {
      parseRssElement(xml, options);
    } else {
      xml.skipCurrentElement();
    }
  }

  if (xml.hasError()) return false;
  for (auto it = options.cbegin(); it != options.cend(); ++it)
    if (!onlyMissingOptions || !settings.hasOption(it.key()))
      settings.setOption(it.key(), it.value());
  return true;
}

QList<anime::Settings> readAnimeSettings(const std::string& path) {
  base::XmlFileReader xml;

  if (!xml.open(QString::fromStdString(path), removeInvalidCharacterReferences)) {
    qCritical() << xml.file().errorString();
    return {};
  }

  if (!xml.readElement(u"settings")) {
    xml.raiseError("Invalid settings file.");
  }

  QList<anime::Settings> items;

  while (xml.readNextStartElement()) {
    if (xml.name() == u"anime") {
      parseAnimeItemsElement(xml, items);
    } else {
      xml.skipCurrentElement();
    }
  }

  if (xml.hasError()) {
    qCritical() << xml.errorString();
  }

  return items;
}

void parseAnnounceElement(QXmlStreamReader& xml, QMap<QString, QVariant>& options) {
  using namespace taiga::opt;
  while (xml.readNextStartElement()) {
    if (xml.name() == u"discord") {
      for (const auto& [old, key] :
           QList<QPair<QString, const char*>>{{"enabled", discordEnabled.key},
                                              {"groupenabled", discordShowGroup.key},
                                              {"timeenabled", discordShowTime.key},
                                              {"usernameenabled", discordShowUsername.key}})
        if (xml.attributes().hasAttribute(old))
          options.insert(
              key, xml.attributes().value(old) == u"true" || xml.attributes().value(old) == u"1");
      if (xml.attributes().hasAttribute(u"applicationid"))
        options.insert(discordApplicationId.key, XML_ATTR(u"applicationid").toString());
    } else if (xml.name() == u"http") {
      if (xml.attributes().hasAttribute(u"enabled"))
        options.insert(httpEnabled.key, XML_ATTR_BOOL(u"enabled"));
      if (xml.attributes().hasAttribute(u"url"))
        options.insert(httpUrl.key, XML_ATTR(u"url").toString());
      if (xml.attributes().hasAttribute(u"format"))
        options.insert(httpFormat.key, XML_ATTR(u"format").toString());
    }
    xml.skipCurrentElement();
  }
}

void parseTorrentFilters(QXmlStreamReader& xml, QMap<QString, QVariant>& options) {
  using namespace track::feed;
  if (xml.attributes().hasAttribute(u"enabled"))
    options.insert(taiga::opt::torrentFiltersEnabled.key, XML_ATTR_BOOL(u"enabled"));
  static const QMap<QString, FilterElement> elements{
      {"meta_id", FilterElement::AnimeId},
      {"meta_status", FilterElement::AnimeStatus},
      {"meta_type", FilterElement::AnimeType},
      {"meta_episodes", FilterElement::AnimeEpisodeCount},
      {"meta_date_start", FilterElement::DateStarted},
      {"meta_date_end", FilterElement::DateFinished},
      {"user_tags", FilterElement::ListNotes},
      {"user_status", FilterElement::ListStatus},
      {"local_episode_available", FilterElement::EpisodeAvailable},
      {"episode_title", FilterElement::AnimeTitle},
      {"episode_number", FilterElement::EpisodeNumber},
      {"episode_version", FilterElement::EpisodeVersion},
      {"episode_group", FilterElement::Group},
      {"episode_video_resolution", FilterElement::Resolution},
      {"episode_video_type", FilterElement::VideoType},
      {"file_title", FilterElement::FileTitle},
      {"file_category", FilterElement::FileCategory},
      {"file_description", FilterElement::FileDescription},
      {"file_link", FilterElement::FileLink},
      {"file_size", FilterElement::FileSize}};
  const QStringList actions{"discard", "select", "prefer"}, matches{"all", "any"},
      filterOptions{"default", "deactivate", "hide"};
  const QStringList operators{"equals", "notequals",  "gt",       "ge",       "lt",
                              "le",     "beginswith", "endswith", "contains", "notcontains"};
  QList<Filter> filters;
  while (xml.readNextStartElement()) {
    if (xml.name() != u"item") {
      xml.skipCurrentElement();
      continue;
    }
    Filter filter;
    filter.name = XML_ATTR(u"name").toString();
    const int action = actions.indexOf(XML_ATTR(u"action").toString().toLower());
    const int match = matches.indexOf(XML_ATTR(u"match").toString().toLower());
    const int option = filterOptions.indexOf(XML_ATTR(u"option").toString().toLower());
    bool valid = action >= 0 && match >= 0 && option >= 0;
    filter.action = static_cast<FilterAction>(std::max(0, action));
    filter.match = static_cast<FilterMatch>(std::max(0, match));
    filter.option = static_cast<FilterOption>(std::max(0, option));
    filter.enabled = XML_ATTR_BOOL(u"enabled");
    while (xml.readNextStartElement()) {
      if (xml.name() == u"anime") {
        const int id = XML_ATTR_INT(u"id");
        if (id > 0)
          filter.animeIds.append(id);
        else
          valid = false;
      } else if (xml.name() == u"condition") {
        const auto element = elements.constFind(XML_ATTR(u"element").toString().toLower());
        const int op = operators.indexOf(XML_ATTR(u"operator").toString().toLower());
        if (element == elements.cend() || op < 0)
          valid = false;
        else
          filter.conditions.append(
              {*element, static_cast<FilterOperator>(op), XML_ATTR(u"value").toString()});
      } else
        valid = false;
      xml.skipCurrentElement();
    }
    if (!valid) {
      filter.enabled = false;
      filter.name.prepend("Needs review: ");
    }
    filters.append(filter);
  }
  options.insert(taiga::opt::torrentFilters.key,
                 QString::fromUtf8(QJsonDocument(toJson(filters)).toJson(QJsonDocument::Compact)));
}

void parseRssElement(QXmlStreamReader& xml, QMap<QString, QVariant>& options) {
  using namespace taiga::opt;
  while (xml.readNextStartElement()) {
    if (xml.name() != u"torrent") {
      xml.skipCurrentElement();
      continue;
    }
    while (xml.readNextStartElement()) {
      if (xml.name() == u"filter") {
        parseTorrentFilters(xml, options);
        continue;
      }
      if (xml.name() == u"source" && xml.attributes().hasAttribute(u"address"))
        options.insert(torrentSource.key, XML_ATTR(u"address").toString());
      else if (xml.name() == u"search" && xml.attributes().hasAttribute(u"address"))
        options.insert(torrentSearchSource.key, XML_ATTR(u"address").toString());
      else if (xml.name() == u"options") {
        if (xml.attributes().hasAttribute(u"autocheck"))
          options.insert(torrentAutoCheck.key, XML_ATTR_BOOL(u"autocheck"));
        if (xml.attributes().hasAttribute(u"checkinterval"))
          options.insert(torrentAutoCheckMinutes.key,
                         std::clamp(XML_ATTR_INT(u"checkinterval"), 5, 1440));
        if (xml.attributes().hasAttribute(u"newaction"))
          options.insert(torrentNewAction.key, XML_ATTR_INT(u"newaction") == 2
                                                   ? int(TorrentNewAction::Download)
                                                   : int(TorrentNewAction::Notify));
      } else if (xml.name() == u"application" && XML_ATTR_INT(u"mode") == 2) {
        options.insert(torrentApp.key, XML_ATTR(u"path").toString());
      }
      xml.skipCurrentElement();
    }
  }
}

void parseAccountElement(QXmlStreamReader& xml, const taiga::Settings& settings,
                         const taiga::Accounts& accounts) {
  while (xml.readNextStartElement()) {
    if (xml.name() == u"update") {
      settings.setService(XML_ATTR_STR(u"activeservice"));
      settings.setUpdateDelay(std::chrono::seconds{XML_ATTR_INT(u"delay")});
      settings.setUpdatePauseWhenUnfocused(XML_ATTR_BOOL(u"checkplayer"));
      settings.setUpdateLibraryOnly(XML_ATTR_BOOL(u"outofroot"));
      settings.setUpdateTrigger(XML_ATTR_BOOL(u"waitplayer") ? track::UpdateTrigger::OnPlayerClose
                                                             : track::UpdateTrigger::AfterDelay);
      xml.skipCurrentElement();

    } else if (xml.name() == u"anilist") {
      accounts.setAnilistUsername(XML_ATTR_STR(u"username"));
      accounts.setAnilistToken(XML_ATTR_STR(u"token"));
      xml.skipCurrentElement();

    } else if (xml.name() == u"kitsu") {
      accounts.setKitsuEmail(XML_ATTR_STR(u"email"));
      accounts.setKitsuUsername(XML_ATTR_STR(u"username"));
      accounts.setKitsuPassword(XML_ATTR_STR(u"password"));
      xml.skipCurrentElement();

    } else if (xml.name() == u"myanimelist") {
      accounts.setMyanimelistUsername(XML_ATTR_STR(u"username"));
      accounts.setMyanimelistAccessToken(XML_ATTR_STR(u"accesstoken"));
      accounts.setMyanimelistRefreshToken(XML_ATTR_STR(u"refreshtoken"));
      xml.skipCurrentElement();

    } else {
      xml.skipCurrentElement();
    }
  }
}

void parseAnimeElement(QXmlStreamReader& xml, const taiga::Settings& settings) {
  std::vector<std::string> libraryFolders;

  while (xml.readNextStartElement()) {
    if (xml.name() == u"folders") {
      while (xml.readNextStartElement()) {
        if (xml.name() == u"root") {
          libraryFolders.push_back(XML_ATTR_STR(u"folder"));
          xml.skipCurrentElement();
        } else {
          xml.skipCurrentElement();
        }
      }

    } else {
      xml.skipCurrentElement();
    }
  }

  settings.setLibraryFolders(libraryFolders);
}

void parseAnimeItemsElement(QXmlStreamReader& xml, QList<anime::Settings>& items) {
  while (xml.readNextStartElement()) {
    if (xml.name() == u"items") {
      while (xml.readNextStartElement()) {
        if (xml.name() == u"item") {
          const int id = XML_ATTR_INT(u"id");
          auto folder = XML_ATTR_STR(u"folder");
          auto synonyms = toVector(XML_ATTR(u"titles").toString().split("; ", Qt::SkipEmptyParts));
          if (id != anime::kUnknownId && (!folder.empty() || !synonyms.empty())) {
            items.push_back({
                .id = id,
                .folder = std::move(folder),
                .synonyms = std::move(synonyms),
            });
          }
          xml.skipCurrentElement();
        } else {
          xml.skipCurrentElement();
        }
      }
    } else {
      xml.skipCurrentElement();
    }
  }
}

void parseRecognitionElement(QXmlStreamReader& xml, const taiga::Settings& settings) {
  while (xml.readNextStartElement()) {
    if (xml.name() == u"mediaplayers") {
      std::vector<std::string> disabledPlayers;
      while (xml.readNextStartElement()) {
        if (xml.name() == u"player") {
          if (!XML_ATTR_BOOL(u"enabled")) {
            disabledPlayers.push_back(XML_ATTR_STR(u"name"));
          }
          xml.skipCurrentElement();
        } else {
          xml.skipCurrentElement();
        }
      }
      settings.setDisabledMediaPlayers(disabledPlayers);

    } else if (xml.name() == u"general") {
      const auto seconds = std::chrono::seconds{XML_ATTR_INT(u"detectioninterval")};
      settings.setMediaDetectionInterval(seconds);
      xml.skipCurrentElement();

    } else if (xml.name() == u"streaming") {
      settings.setStreamingMediaEnabled(XML_ATTR_BOOL(u"enabled"));
      xml.skipCurrentElement();

    } else {
      xml.skipCurrentElement();
    }
  }
}

void parseProgramElement(QXmlStreamReader& xml, const taiga::Settings& settings,
                         QMap<QString, QVariant>& options, bool onlyMissingOptions) {
  using namespace taiga::opt;
  const auto boolean = [&](QStringView attribute, const char* key, bool invert = false) {
    if (!xml.attributes().hasAttribute(attribute)) return;
    const auto value = xml.attributes().value(attribute);
    options.insert(key, (value == u"true" || value == u"1") != invert);
  };
  while (xml.readNextStartElement()) {
    if (xml.name() == u"list") {
      while (xml.readNextStartElement()) {
        if (xml.name() == u"action") {
          if (!onlyMissingOptions && xml.attributes().hasAttribute(u"titlelang")) {
            const auto language = XML_ATTR_STR(u"titlelang");
            settings.setTitleLanguage(language == "english"  ? anime::TitleLanguage::English
                                      : language == "native" ? anime::TitleLanguage::Native
                                                             : anime::TitleLanguage::Romaji);
          }
          const QList<ListAction> actions{ListAction::Nothing,     ListAction::EditEntry,
                                          ListAction::OpenFolder,  ListAction::PlayNextEpisode,
                                          ListAction::ViewDetails, ListAction::OpenPage};
          for (const auto& [attribute, key] : QList<QPair<QString, const char*>>{
                   {"doubleclick", listDoubleClick.key}, {"middleclick", listMiddleClick.key}}) {
            bool ok = false;
            const int action = xml.attributes().value(attribute).toInt(&ok);
            if (ok && action >= 0 && action < actions.size())
              options.insert(key, int(actions[action]));
          }
          xml.skipCurrentElement();
        } else if (xml.name() == u"filter") {
          while (xml.readNextStartElement()) {
            if (xml.name() == u"episodes") {
              boolean(u"highlight", highlightNewEpisodes.key);
              boolean(u"highlightedontop", newEpisodesOnTop.key);
            }
            xml.skipCurrentElement();
          }
        } else
          xml.skipCurrentElement();
      }
    } else if (xml.name() == u"general") {
      boolean(u"enablesharing", sharingEnabled.key);
      boolean(u"close", closeToTray.key);
      boolean(u"minimize", minimizeToTray.key);
      boolean(u"hidesidebar", showSidebar.key, true);
      boolean(u"enablesync", "sync.enabled");
      boolean(u"enablerecognition", "track.detection.enabled");
      if (xml.attributes().hasAttribute(u"externallinks"))
        options.insert(externalLinks.key, XML_ATTR(u"externallinks").toString());
      xml.skipCurrentElement();
    } else if (xml.name() == u"startup") {
      boolean(u"minimize", startMinimized.key);
      boolean(u"checkversion", checkUpdatesOnStartup.key);
      boolean(u"checkeps", scanOnStartup.key);
      xml.skipCurrentElement();
    } else if (xml.name() == u"proxy" && !onlyMissingOptions) {
      const auto raw = XML_ATTR(u"host").toString();
      const QUrl url{raw.contains(u"://") ? raw : u"//" + raw};
      settings.setProxyType(url.scheme().toLower().contains(u"socks5") ? QNetworkProxy::Socks5Proxy
                                                                       : QNetworkProxy::HttpProxy);
      settings.setProxyHost(url.host().toStdString());
      settings.setProxyPort(url.port());
      settings.setProxyUsername(XML_ATTR_STR(u"username"));
      settings.setProxyPassword(XML_ATTR_STR(u"password"));
      xml.skipCurrentElement();
    } else
      xml.skipCurrentElement();
  }
}

}  // namespace compat::v1
