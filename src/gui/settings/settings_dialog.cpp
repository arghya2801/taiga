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

#include "settings_dialog.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "base/string.hpp"
#include "gui/settings/settings_page_accounts.hpp"
#include "gui/settings/settings_page_advanced.hpp"
#include "gui/settings/settings_page_application.hpp"
#include "gui/settings/settings_page_library.hpp"
#include "gui/settings/settings_page_media_players.hpp"
#include "gui/settings/settings_page_options.hpp"
#include "gui/settings/settings_page_recognition.hpp"
#include "gui/settings/settings_page_streaming.hpp"
#include "gui/torrents/filter_editor.hpp"
#include "gui/utils/image_provider.hpp"
#include "gui/utils/theme.hpp"
#include "track/feed.hpp"
#include "ui_settings_dialog.h"

#ifdef Q_OS_WINDOWS
#include "gui/platforms/windows.hpp"
#endif

namespace gui {

namespace {

[[maybe_unused]] constexpr auto kWindowsRunKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

}  // namespace

constexpr int kPageRole = Qt::UserRole;

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent), ui_(new Ui::SettingsDialog) {
  ui_->setupUi(this);

#ifdef Q_OS_WINDOWS
  enableMicaBackground(this);
#endif

  ui_->treeWidget->setIndentation(22);

  const auto add_item = [this](QString icon, QString text, QWidget* page = nullptr) {
    auto item = new QTreeWidgetItem(ui_->treeWidget, QStringList(text));
    item->setIcon(0, theme.getIcon(icon));
    item->setSizeHint(0, QSize{0, 24});
    item->setData(0, kPageRole, QVariant::fromValue(page));
    if (!page) item->setDisabled(true);
    return item;
  };

  const auto add_child = [this](QTreeWidgetItem* parent, QString text, QWidget* page = nullptr) {
    auto item = new QTreeWidgetItem(parent, QStringList(text));
    item->setData(0, kPageRole, QVariant::fromValue(page));
    if (!page) item->setDisabled(true);
  };

  add_item("account_circle", "Accounts", ui_->accountsPage);
  auto* startupPage = new SettingsPageOptions(ui_, this);
  {
    using namespace taiga::opt;
    startupPage->addHeading(tr("Startup"));
#ifdef Q_OS_WINDOWS
    auto* startWithWindows = new QCheckBox(tr("Start Taiga with Windows"));
    startupPage->addRow(
        {}, startWithWindows,
        [startWithWindows] {
          const QSettings run{kWindowsRunKey, QSettings::NativeFormat};
          startWithWindows->setChecked(run.contains("Taiga"));
        },
        [startWithWindows] {
          QSettings run{kWindowsRunKey, QSettings::NativeFormat};
          if (startWithWindows->isChecked()) {
            run.setValue("Taiga", u"\"%1\""_s.arg(QDir::toNativeSeparators(
                                      QCoreApplication::applicationFilePath())));
          } else {
            run.remove("Taiga");
          }
        });
#endif
    startupPage->addCheck(startMinimized, tr("Start minimized"));
    startupPage->addCheck(checkUpdatesOnStartup, tr("Check for updates"));
    startupPage->addCheck(scanOnStartup, tr("Scan library folders for available episodes"));
    startupPage->addHeading(tr("System tray"));
    startupPage->addCheck(minimizeToTray, tr("Minimize to the system tray"));
    startupPage->addCheck(closeToTray, tr("Close to the system tray"));
  }

  auto* linksPage = new SettingsPageOptions(ui_, this);
  {
    auto* links = new QPlainTextEdit;
    links->setMinimumHeight(180);
    linksPage->addRow(
        tr("External links:"), links,
        [links] { links->setPlainText(taiga::opt::externalLinks.get()); },
        [links] { taiga::opt::externalLinks.set(links->toPlainText().trimmed()); });
    linksPage->addNote(tr("Shown in the profile menu. One \"Name|URL\" per line, and a line "
                          "with just - for a separator."));
  }

  auto* notificationsPage = new SettingsPageOptions(ui_, this);
  {
    using namespace taiga::opt;
    notificationsPage->addHeading(tr("List updates"));
    notificationsPage->addCheck(askBeforeUpdating, tr("Ask before updating my list"));
    notificationsPage->addHeading(tr("Notifications"));
    notificationsPage->addCheck(notifyRecognized, tr("Notify me when an episode is recognized"));
    notificationsPage->addCheck(notifyNotRecognized,
                                tr("Notify me when media can't be recognized"));
    notificationsPage->addHeading(tr("File names"));
    notificationsPage->addText(ignoredStrings, tr("Ignored strings:"),
                               tr("Comma-separated, e.g. [Batch], (Uncensored)"));
    notificationsPage->addNote(
        tr("These are removed from file names before they're parsed. Use this for release "
           "tags that get mistaken for part of the title."));
  }

  {
    auto item = add_item("web_asset", "Application", ui_->applicationPage);
    add_child(item, "Startup and tray", startupPage->widget());
    add_child(item, "Links", linksPage->widget());
  }
  auto* animeListPage = new SettingsPageOptions(ui_, this);
  {
    using namespace taiga::opt;
    const QStringList actions{tr("View details"),      tr("Edit list entry"),
                              tr("Play next episode"), tr("Open folder"),
                              tr("Open anime page"),   tr("Do nothing")};
    animeListPage->addHeading(tr("Mouse actions"));
    animeListPage->addChoice(listDoubleClick, tr("Double-click:"), actions);
    animeListPage->addChoice(listMiddleClick, tr("Middle-click:"), actions);
    animeListPage->addHeading(tr("New episodes"));
    animeListPage->addCheck(highlightNewEpisodes,
                            tr("Highlight anime with new episodes in library folders"));
    animeListPage->addCheck(newEpisodesOnTop, tr("Show highlighted anime above the others"));
    animeListPage->addNote(
        tr("New episodes are found by scanning your library folders (File > Scan available "
           "episodes). To choose which columns are shown, right-click a column header."));
  }
  add_item("list_alt", "Anime List", animeListPage->widget());
  add_item("folder", "Library", ui_->libraryPage);
  {
    auto item = add_item("check_circle", "Recognition", ui_->recognitionPage);
    add_child(item, "Media players", ui_->mediaPlayersPage);
    add_child(item, "Streaming", ui_->streamingPage);
    add_child(item, "Updates and notifications", notificationsPage->widget());
  }
  auto* discordPage = new SettingsPageOptions(ui_, this);
  {
    using namespace taiga::opt;
    discordPage->addCheck(discordEnabled, tr("Show what I'm watching in my Discord status"));
    discordPage->addHeading(tr("Show"));
    discordPage->addCheck(discordShowGroup, tr("Release group"));
    discordPage->addCheck(discordShowTime, tr("Elapsed time"));
    discordPage->addCheck(discordShowUsername, tr("My username"));
    discordPage->addNote(
        tr("Discord must be running on this computer. Anime marked as private are never "
           "shared."));
  }
  auto* httpPage = new SettingsPageOptions(ui_, this);
  {
    using namespace taiga::opt;
    httpPage->addCheck(httpEnabled, tr("Send what I'm watching to a web address"));
    httpPage->addText(httpUrl, tr("URL:"), u"https://example.com/taiga"_s);
    auto* format = new QPlainTextEdit;
    format->setMinimumHeight(90);
    httpPage->addRow(
        tr("Format:"), format, [format] { format->setPlainText(httpFormat.get()); },
        [format] {
          const auto text = format->toPlainText().trimmed();
          httpFormat.set(text.isEmpty() ? QString::fromUtf8(httpFormat.fallback) : text);
        });
    httpPage->addNote(
        tr("Sent as a form POST when playback starts and stops. Variables: %title%, "
           "%episode%, %total%, %watched%, %score%, %group%, %resolution%, %image%, "
           "%animeurl%, %user%, %playstatus%. Functions like $if(a,b,c) work as in v1."));
  }
  {
    auto item = add_item("share", "Sharing", discordPage->widget());
    add_child(item, "Discord", discordPage->widget());
    add_child(item, "HTTP", httpPage->widget());
  }
  auto* torrentsPage = new SettingsPageOptions(ui_, this);
  {
    using namespace taiga::opt;
    torrentsPage->addHeading(tr("Sources"));
    torrentsPage->addText(torrentSource, tr("New torrents:"), tr("RSS feed URL"));
    torrentsPage->addText(torrentSearchSource, tr("Search:"),
                          tr("RSS feed URL, with %title% for the anime title"));
    torrentsPage->addHeading(tr("Automatic check"));
    torrentsPage->addCheck(torrentAutoCheck, tr("Check for new torrents automatically"));
    torrentsPage->addSpin(torrentAutoCheckMinutes, tr("Every:"), 5, 1440, tr(" minutes"));
    torrentsPage->addChoice(torrentNewAction, tr("When torrents are selected:"),
                            {tr("Notify me"), tr("Download them")});
    torrentsPage->addHeading(tr("Downloads"));
    torrentsPage->addText(torrentApp, tr("Torrent app:"),
                          tr("Leave empty to use the default app for torrents"));
    torrentsPage->addText(torrentAppArgs, tr("Arguments:"), u"\"%file%\""_s);
    torrentsPage->addNote(
        tr("%file% is the torrent file or magnet link, %folder% the anime's folder from its "
           "settings. For qBittorrent, \"%file%\" --save-path=\"%folder%\" downloads into "
           "that folder."));
  }
  auto* filtersPage = new SettingsPageOptions(ui_, this);
  {
    auto* editor = new FilterListEditor;
    filtersPage->addCheck(taiga::opt::torrentFiltersEnabled, tr("Use filters"));
    filtersPage->addRow(
        {}, editor, [editor] { editor->setFilters(track::feed::aggregator()->filters()); },
        [editor] { track::feed::aggregator()->setFilters(editor->filters()); });
    filtersPage->addNote(
        tr("Filters run from top to bottom. Discard and select filters go first, then "
           "preferences pick among releases of the same episode. Double-click a filter to "
           "edit it."));
  }
  {
    auto item = add_item("rss_feed", "Torrents", torrentsPage->widget());
    add_child(item, "Filters", filtersPage->widget());
  }
  {
    auto item = add_item("warning", "Advanced", ui_->advancedPage);
    add_child(item, "Cache", [this] {
      auto* page = new QWidget(ui_->stackedWidget);
      auto* layout = new QVBoxLayout(page);
      auto* label = new QLabel(page);
      const auto update = [label] {
        label->setText(tr("Cached images use %1 on disk.")
                           .arg(QLocale().formattedDataSize(imageProvider.cacheSize())));
      };
      update();
      auto* button = new QPushButton(tr("Clear cache"), page);
      connect(button, &QPushButton::clicked, page, [update] {
        imageProvider.clearCache();
        update();
      });
      layout->addWidget(label);
      layout->addWidget(button, 0, Qt::AlignLeft);
      layout->addStretch();
      ui_->stackedWidget->addWidget(page);
      return page;
    }());
  }

  ui_->treeWidget->expandAll();

  connect(ui_->treeWidget, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (current) {
              auto text = current->text(0);
              if (current->parent()) {
                text = u"%1 / %2"_s.arg(current->parent()->text(0), text);
              }
              ui_->titleLabel->setText(text);

              auto page = current->data(0, kPageRole).value<QWidget*>();
              ui_->stackedWidget->setCurrentWidget(page ? page : ui_->todoPage);
            }
          });

  ui_->treeWidget->setCurrentItem(ui_->treeWidget->topLevelItem(0));

  pages_ = {
      // clang-format off
      new SettingsPageAccounts(ui_, this),
      new SettingsPageAdvanced(ui_, this),
      new SettingsPageApplication(ui_, this),
      new SettingsPageLibrary(ui_, this),
      new SettingsPageMediaPlayers(ui_, this),
      new SettingsPageRecognition(ui_, this),
      new SettingsPageStreaming(ui_, this),
      animeListPage,
      startupPage,
      notificationsPage,
      linksPage,
      discordPage,
      httpPage,
      torrentsPage,
      filtersPage,
      // clang-format on
  };
  for (auto page : pages_) {
    page->load();
  }
}

void SettingsDialog::show(QWidget* parent) {
  auto dlg = new SettingsDialog(parent);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setModal(true);
  dlg->QDialog::show();
}

void SettingsDialog::accept() {
  for (const auto page : pages_) {
    page->apply();
  }
  // Settings that other parts of the app only read on startup.
  track::feed::aggregator()->updateTimer();
  track::feed::aggregator()->refilter();
  QDialog::accept();
}

}  // namespace gui
