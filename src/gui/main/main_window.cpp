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

#include "main_window.hpp"

#include <QDesktopServices>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QtWidgets>
#include <algorithm>
#include <optional>

#include "base/string.hpp"
#include "gui/common/spinner_widget.hpp"
#include "gui/history/history_widget.hpp"
#include "gui/library/library_widget.hpp"
#include "gui/list/list_widget.hpp"
#include "gui/manga/manga_widget.hpp"
#include "gui/main/about_dialog.hpp"
#include "gui/main/navigation_controller.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/main/now_playing_widget.hpp"
#include "gui/main/status_bar.hpp"
#include "gui/main/status_bar_controller.hpp"
#include "gui/search/search_widget.hpp"
#include "gui/settings/settings_dialog.hpp"
#include "gui/stats/stats_widget.hpp"
#include "gui/torrents/torrents_widget.hpp"
#include "gui/utils/format.hpp"
#include "gui/utils/theme.hpp"
#include "gui/utils/tray_icon.hpp"
#include "gui/utils/widgets.hpp"
#include "media/anime_db.hpp"
#include "media/anime_list.hpp"
#include "media/anime_utils.hpp"
#include "sync/anilist/anilist.hpp"
#include "sync/kitsu/kitsu.hpp"
#include "sync/myanimelist/myanimelist.hpp"
#include "sync/queue.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/application.hpp"
#include "taiga/session.hpp"
#include "taiga/network.hpp"
#include "taiga/options.hpp"
#include "taiga/settings.hpp"
#include "taiga/sharing.hpp"
#include "taiga/version.hpp"
#include "track/media.hpp"
#include "track/play.hpp"
#include "track/feed.hpp"
#include "track/scanner.hpp"
#include "track/update_session.hpp"
#include "ui_main_window.h"

#ifdef Q_OS_WINDOWS
#include "gui/platforms/windows.hpp"
#endif

namespace gui {

MainWindow::MainWindow() : QMainWindow(), ui_(new Ui::MainWindow) {
  ui_->setupUi(this);

  ui_->menubar->hide();

  // Pages added in code go after the ones in the .ui file, in `MainWindowPage` order.
  m_statsPage = new QWidget(ui_->stackedWidget);
  ui_->stackedWidget->insertWidget(static_cast<int>(MainWindowPage::Stats), m_statsPage);

#ifdef Q_OS_WINDOWS
  enableMicaBackground(this);
#endif

  if (const auto geometry = taiga::session.mainWindowGeometry(); !geometry.isEmpty()) {
    restoreGeometry(geometry);
    centerWidgetToScreen(this);
  }

  // Do not call `init()` here, as it relies on the main window pointer being
  // available through the application instance.
}

MainWindow* mainWindow() {
  return taiga::app()->mainWindow();
}

NavigationWidget* MainWindow::navigation() const {
  return m_navigationWidget;
}

NowPlayingWidget* MainWindow::nowPlaying() const {
  return m_nowPlayingWidget;
}

QLineEdit* MainWindow::searchBox() const {
  return m_searchBox;
}

StatusBarController* MainWindow::statusBarController() const {
  return m_statusBarController;
}

Ui::MainWindow* MainWindow::ui() const {
  return ui_;
}

void MainWindow::init() {
  initActions();
  initIcons();
  initTrayIcon();
  initToolbar();
  initStatusbar();
  initNavigation();
  initNowPlaying();
  updateTitle();

  if (taiga::opt::scanOnStartup.get() && !taiga::settings.libraryFolders().empty()) {
    scanAvailableEpisodes();
  }
  auto* feeds = track::feed::aggregator();
  feeds->updateTimer();
  connect(feeds, &track::feed::Aggregator::newItemsFound, this,
          [this](const QList<track::feed::Item>& items) {
            const bool downloaded = taiga::opt::torrentNewAction.get() ==
                                    static_cast<int>(taiga::opt::TorrentNewAction::Download);
            QStringList titles;
            for (const auto& item : items.mid(0, 5)) titles.append(item.title);
            m_trayIcon->showMessage(
                downloaded ? tr("Downloading %n new torrent(s)", nullptr, items.size())
                           : tr("%n new torrent(s) available", nullptr, items.size()),
                titles.join(u'\n'));
          });

  if (taiga::opt::checkUpdatesOnStartup.get()) {
    QTimer::singleShot(std::chrono::seconds{5}, this, [this] { checkForUpdates(true); });
  }
}

void MainWindow::initActions() {
  ui_->actionProfile->setToolTip(tr("Profile and links"));
  ui_->actionSynchronize->setToolTip(
      tr("Synchronize with %1").arg(sync::serviceName(sync::currentServiceId())));

  connect(ui_->actionAddNewFolder, &QAction::triggered, this, &MainWindow::addNewFolder);
  connect(ui_->actionExit, &QAction::triggered, this, &MainWindow::quit, Qt::QueuedConnection);
  connect(ui_->actionScanAvailableEpisodes, &QAction::triggered, this,
          &MainWindow::scanAvailableEpisodes);
  connect(ui_->actionPlayNextEpisode, &QAction::triggered, this, &MainWindow::playNextEpisode);
  connect(ui_->actionPlayRandomAnime, &QAction::triggered, this, &MainWindow::playRandomAnime);
  ui_->actionCheckForUpdates->setEnabled(true);
  connect(ui_->actionCheckForUpdates, &QAction::triggered, this,
          [this] { checkForUpdates(false); });

  connect(&track::availableEpisodes, &track::AvailableEpisodes::scanFinished, this,
          [this](int count) {
            m_statusBarController->showMessage({
                .source = StatusBarController::Source::Library,
                .text = tr("Found %n episode(s) in library folders.", nullptr, count),
                .spin = false,
            });
          });
  connect(ui_->actionSettings, &QAction::triggered, this, [this]() { SettingsDialog::show(this); });
  connect(ui_->actionAbout, &QAction::triggered, this, &MainWindow::about);
  connect(ui_->actionDonate, &QAction::triggered, this, &MainWindow::donate);
  connect(ui_->actionSupport, &QAction::triggered, this, &MainWindow::support);
  connect(ui_->actionDisplayWindow, &QAction::triggered, this, &MainWindow::displayWindow);
  connect(ui_->actionSynchronize, &QAction::triggered, this, &MainWindow::synchronize);

  ui_->actionToggleDetection->setChecked(track::media::detection()->isEnabled());
  connect(ui_->actionToggleDetection, &QAction::toggled, this,
          [](const bool checked) { track::media::detection()->setEnabled(checked); });

  ui_->actionToggleSharing->setChecked(taiga::opt::sharingEnabled.get());
  connect(ui_->actionToggleSharing, &QAction::toggled, this, [](const bool checked) {
    if (!checked) taiga::sharing::clear();
    taiga::opt::sharingEnabled.set(checked);
    if (checked) taiga::sharing::announce(track::media::detection()->getCurrentEpisode());
  });

  ui_->actionToggleSynchronization->setChecked(taiga::settings.syncEnabled());
  connect(ui_->actionToggleSynchronization, &QAction::toggled, this,
          [](const bool checked) { taiga::settings.setSyncEnabled(checked); });
}

void MainWindow::initIcons() {
  ui_->menuLibraryFolders->setIcon(theme.getIcon("folder"));
  ui_->menuExport->setIcon(theme.getIcon("export_notes"));

  ui_->actionAddNewFolder->setIcon(theme.getIcon("create_new_folder"));
  ui_->actionAbout->setIcon(theme.getIcon("info"));
  ui_->actionBack->setIcon(theme.getIcon("arrow_back"));
  ui_->actionCheckForUpdates->setIcon(theme.getIcon("cloud_download"));
  ui_->actionDonate->setIcon(theme.getIcon("favorite"));
  ui_->actionExit->setIcon(theme.getIcon("logout"));
  ui_->actionForward->setIcon(theme.getIcon("arrow_forward"));
  ui_->actionMenu->setIcon(theme.getIcon("menu"));
  ui_->actionPlayNextEpisode->setIcon(theme.getIcon("skip_next"));
  ui_->actionPlayRandomAnime->setIcon(theme.getIcon("shuffle"));
  ui_->actionProfile->setIcon(theme.getIcon("account_circle"));
  ui_->actionScanAvailableEpisodes->setIcon(theme.getIcon("pageview"));
  ui_->actionSettings->setIcon(theme.getIcon("settings"));
  ui_->actionSupport->setIcon(theme.getIcon("help"));
  ui_->actionSynchronize->setIcon(theme.getIcon("sync"));
}

void MainWindow::initNavigation() {
  m_navigationWidget = new NavigationWidget(this);
  // Connects to m_navigationWidget's signals on construction, so it must come after.
  m_navigationController = new NavigationController(this);

  const bool hasWatching = std::ranges::any_of(anime::db.entries(), [](const auto& entry) {
    return entry.status == anime::list::Status::Watching;
  });
  if (hasWatching) {
    navigateToListStatus(anime::list::Status::Watching);
  } else {
    navigateTo(MainWindowPage::List);
  }

  ui_->splitter->insertWidget(0, m_navigationWidget);
  m_navigationWidget->setVisible(taiga::opt::showSidebar.get());
}

void MainWindow::initNowPlaying() {
  m_nowPlayingWidget = new NowPlayingWidget(ui_->centralWidget);

  ui_->centralWidget->layout()->addWidget(m_nowPlayingWidget);
  m_nowPlayingWidget->hide();
}

void MainWindow::initPage(MainWindowPage page) {
  static QSet<MainWindowPage> initializedPages;

  if (initializedPages.contains(page)) return;

  static const auto init_page = [](QWidget* page, QWidget* widget) {
    const auto layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(widget);
  };

  switch (page) {
    case MainWindowPage::Home:
      break;

    case MainWindowPage::Search:
      m_searchWidget = new SearchWidget(ui_->searchPage);
      init_page(ui_->searchPage, m_searchWidget);
      break;

    case MainWindowPage::List:
      m_listWidget = new ListWidget(ui_->listPage);
      init_page(ui_->listPage, m_listWidget);
      break;

    case MainWindowPage::Manga:
      m_mangaWidget = new MangaWidget(ui_->mangaPage);
      init_page(ui_->mangaPage, m_mangaWidget);
      break;

    case MainWindowPage::History:
      m_historyWidget = new HistoryWidget(ui_->historyPage);
      init_page(ui_->historyPage, m_historyWidget);
      break;

    case MainWindowPage::Library:
      m_libraryWidget = new LibraryWidget(ui_->libraryPage);
      init_page(ui_->libraryPage, m_libraryWidget);
      break;

    case MainWindowPage::Torrents:
      m_torrentsWidget = new TorrentsWidget(ui_->torrentsPage);
      init_page(ui_->torrentsPage, m_torrentsWidget);
      break;

    case MainWindowPage::Profile:
      break;

    case MainWindowPage::Stats:
      init_page(m_statsPage, new StatsWidget(m_statsPage));
      break;
  }

  initializedPages.insert(page);
}

void MainWindow::initStatusbar() {
  const auto statusbar = new StatusBar(this);
  statusbar->setObjectName(ui_->statusbar->objectName());
  setStatusBar(statusbar);
  ui_->statusbar = statusbar;

  const auto spinner = new SpinnerWidget(this);
  const auto spinnerContainer = new QWidget(this);
  const auto spinnerLayout = new QVBoxLayout(spinnerContainer);
  spinnerLayout->setContentsMargins(0, 2, 0, 0);
  spinnerLayout->addWidget(spinner);
  ui_->statusbar->addPermanentWidget(spinnerContainer);

  m_statusBarController = new StatusBarController(this, statusbar, spinner);

  const QList<sync::Service*> services{
      sync::anilist::Service::instance(),
      sync::kitsu::Service::instance(),
      sync::myanimelist::Service::instance(),
  };
  for (auto* service : services) {
    connect(service, &sync::Service::authenticationCompleted, this,
            [this](const bool authenticated) {
              if (!authenticated) return;

              const auto sender_service = qobject_cast<sync::Service*>(sender());
              const auto slug = sync::serviceSlug(sender_service->id()).toStdString();
              const auto username = taiga::accounts.serviceUsername(slug);

              m_statusBarController->showMessage({
                  .source = StatusBarController::Source::Sync,
                  .text = tr("Logged in as %1.").arg(username),
                  .spin = false,
              });
            });
    connect(service, &sync::Service::listEntriesFetched, this, [this]() {
      m_statusBarController->clearMessage(StatusBarController::Source::Sync);
      setEnabled(true);
    });
    connect(service, &sync::Service::errorOccurred, this, [this](const QString& message) {
      const auto sender_service = qobject_cast<sync::Service*>(sender());
      m_statusBarController->showMessage({
          .source = StatusBarController::Source::Sync,
          .text = sync::tagMessage(sender_service->id(), message),
          .spin = false,
      });
      setEnabled(true);
    });
    connect(service, &sync::Service::transferProgress, this,
            [this](const qint64 current, const qint64 total) {
              m_statusBarController->showMessage({
                  .source = StatusBarController::Source::Sync,
                  .text = tr("Synchronizing with %1... (%2)")
                              .arg(sync::serviceName(sync::currentServiceId()))
                              .arg(gui::formatTransferProgress(current, total)),
              });
            });
  }

  connect(&sync::queue, &sync::Queue::changed, this, [this]() {
    if (sync::queue.count() == 0) {
      m_statusBarController->clearMessage(StatusBarController::Source::Sync);
      setEnabled(true);
    }
  });

  connect(&sync::queue, &sync::Queue::processing, this, [this](const int animeId) {
    const auto item = anime::db.item(animeId);
    const auto entry = anime::db.entry(animeId);
    if (!item || !entry) return;

    const auto title = anime::preferredTitle(*item);

    QString text;
    if (entry->pending_delete) {
      text = tr("Deleting list entry... (%1)").arg(title);
    } else if (entry->id == anime::list::kUnknownId) {
      text = tr("Adding to list... (%1)").arg(title);
    } else {
      text = tr("Updating list entry... (%1)").arg(title);
    }

    m_statusBarController->showMessage({
        .source = StatusBarController::Source::Sync,
        .text = text,
    });
  });

  connect(&sync::queue, &sync::Queue::queuedWhileUnauthenticated, this, [this](const int animeId) {
    const auto item = anime::db.item(animeId);
    if (!item) return;

    m_statusBarController->showMessage({
        .source = StatusBarController::Source::Sync,
        .text = tr("%1 is queued for update.").arg(anime::preferredTitle(*item)),
        .spin = false,
    });
  });

  connect(&anime::db, &anime::Database::itemDeleted, this, [this](const int, const QString& title) {
    if (title.isEmpty()) return;

    m_statusBarController->showMessage({
        .source = StatusBarController::Source::Sync,
        .text = tr("Anime removed from database: %1").arg(title),
        .spin = false,
    });
  });
}

void MainWindow::initToolbar() {
  ui_->toolbar->setIconSize(QSize{24, 24});

  // Menu
  {
    const auto button = static_cast<QToolButton*>(ui_->toolbar->widgetForAction(ui_->actionMenu));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setMenu([this]() {
      auto menu = new QMenu(this);
      menu->addAction(ui_->actionToggleDetection);
      menu->addAction(ui_->actionToggleSharing);
      menu->addAction(ui_->actionToggleSynchronization);
      menu->addSeparator();
      menu->addAction(ui_->actionScanAvailableEpisodes);
      menu->addAction(ui_->actionPlayNextEpisode);
      menu->addAction(ui_->actionPlayRandomAnime);
      menu->addSeparator();
      auto* sidebar = menu->addAction(tr("Show sidebar"));
      sidebar->setCheckable(true);
      sidebar->setChecked(taiga::opt::showSidebar.get());
      connect(sidebar, &QAction::toggled, this, [this](bool checked) {
        taiga::opt::showSidebar.set(checked);
        m_navigationWidget->setVisible(checked);
      });
      menu->addSeparator();
      menu->addMenu(ui_->menuHelp);
      menu->addSeparator();
      menu->addAction(ui_->actionExit);
      return menu;
    }());
  }

  // Profile and links, like v1's profile and external links menus
  {
    const auto button =
        static_cast<QToolButton*>(ui_->toolbar->widgetForAction(ui_->actionProfile));
    button->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(this);
    button->setMenu(menu);
    connect(menu, &QMenu::aboutToShow, this, [menu] {
      menu->clear();
      const auto open = [menu](const QString& text, const QString& url) {
        menu->addAction(text, menu, [url] { QDesktopServices::openUrl(QUrl{url}); });
      };

      const auto service = sync::currentServiceId();
      const auto user = QString::fromStdString(
          taiga::accounts.serviceUsername(sync::serviceSlug(service).toStdString()));
      if (user.isEmpty()) {
        menu->addAction(tr("Log in to see your profile links"))->setEnabled(false);
      } else {
        switch (service) {
          case sync::ServiceId::MyAnimeList:
            open(tr("Profile"), u"https://myanimelist.net/profile/%1"_s.arg(user));
            open(tr("Anime list"), u"https://myanimelist.net/animelist/%1"_s.arg(user));
            open(tr("History"), u"https://myanimelist.net/history/%1"_s.arg(user));
            open(tr("Panel"), u"https://myanimelist.net/panel.php"_s);
            break;
          case sync::ServiceId::Kitsu:
            open(tr("Profile"), u"https://kitsu.app/users/%1"_s.arg(user));
            open(tr("Library"), u"https://kitsu.app/users/%1/library"_s.arg(user));
            break;
          case sync::ServiceId::AniList:
            open(tr("Profile"), u"https://anilist.co/user/%1/"_s.arg(user));
            open(tr("Anime list"), u"https://anilist.co/user/%1/animelist"_s.arg(user));
            open(tr("Stats"), u"https://anilist.co/user/%1/stats/anime/overview"_s.arg(user));
            break;
          default:
            break;
        }
      }

      menu->addSeparator();
      for (const auto& line : taiga::opt::externalLinks.get().split(u'\n', Qt::SkipEmptyParts)) {
        if (line.trimmed() == u"-") {
          menu->addSeparator();
        } else if (const auto parts = line.split(u'|'); parts.size() == 2) {
          open(parts[0].trimmed(), parts[1].trimmed());
        }
      }
    });
  }

  // Search box
  {
    m_searchBox = new QLineEdit();
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setFixedWidth(320);
    m_searchBox->setPlaceholderText(tr("Search"));

    const auto before = ui_->actionSettings;
    const auto insertSpacer = [this](QAction* before) {
      ui_->toolbar->insertWidget(before, [this]() {
        auto spacer = new QWidget(this);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return spacer;
      }());
    };

    insertSpacer(before);
    ui_->toolbar->insertWidget(before, m_searchBox);
    insertSpacer(before);
  }
}

void MainWindow::initTrayIcon() {
  auto menu = new QMenu(this);
  menu->addAction(ui_->actionDisplayWindow);
  menu->setDefaultAction(ui_->actionDisplayWindow);
  menu->addSeparator();
  menu->addAction(ui_->actionSettings);
  menu->addSeparator();
  menu->addAction(ui_->actionExit);

  m_trayIcon = new TrayIcon(this, windowIcon(), menu);

  connect(m_trayIcon, &TrayIcon::activated, this, &MainWindow::displayWindow);
  connect(m_trayIcon, &TrayIcon::messageClicked, this, &MainWindow::displayWindow);

  connect(track::updateSession(), &track::UpdateSession::confirmationRequested, this,
          [this](const track::UpdateState& state) {
            if (isActiveWindow()) return;

            const auto episode = track::media::detection()->getCurrentEpisode();
            const auto item = episode ? anime::db.item(episode->animeId()) : nullptr;
            if (!item) return;

            const auto title = QString::fromStdString(anime::preferredTitle(*item));
            m_trayIcon->showMessage(
                tr("Confirm list update"),
                state.reason == track::UpdateDecision::Reason::AskEnabled
                    ? tr("Update %1 to episode %2?").arg(title).arg(state.episode)
                    : tr("%1: episode %2 is ahead of your progress (%3).")
                          .arg(title)
                          .arg(state.episode)
                          .arg(state.previousEpisode));
          });

  connect(track::media::detection(), &track::media::Detection::currentEpisodeChanged, this,
          [this](std::optional<track::Episode> episode) {
            taiga::sharing::announce(episode);

            if (!episode) {
              m_trayIcon->setBadge(TrayIcon::Badge::None);
            } else {
              const auto item = anime::db.item(episode->animeId());
              m_trayIcon->setBadge(item ? TrayIcon::Badge::Success : TrayIcon::Badge::Error);

              const auto number =
                  QString::fromStdString(episode->element(anitomy::ElementKind::Episode));
              if (item && taiga::opt::notifyRecognized.get()) {
                const auto title = QString::fromStdString(anime::preferredTitle(*item));
                m_trayIcon->showMessage(tr("Now watching"),
                                        number.isEmpty() ? title : tr("%1 #%2").arg(title, number));
              } else if (!item && taiga::opt::notifyNotRecognized.get()) {
                m_trayIcon->showMessage(
                    tr("Media not recognized"),
                    tr("%1. Right-click an anime and choose \"Match now playing\" to link it.")
                        .arg(QString::fromStdString(
                            episode->element(anitomy::ElementKind::Title, "?"))));
              }
            }
          });
}

void MainWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  if (event->type() == QEvent::WindowStateChange && isMinimized() &&
      taiga::opt::minimizeToTray.get()) {
    QTimer::singleShot(0, this, &QWidget::hide);
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  taiga::session.setMainWindowGeometry(saveGeometry());
  if (!m_quitting && taiga::opt::closeToTray.get()) {
    hide();
    event->ignore();
    return;
  }
  if (m_listWidget) m_listWidget->saveState();
  if (m_searchWidget) m_searchWidget->saveState();
  event->accept();
}

void MainWindow::addNewFolder() {
  constexpr auto options =
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks | QFileDialog::ReadOnly;

  const auto directory = QFileDialog::getExistingDirectory(this, tr("Add New Folder"), "", options);

  if (!directory.isEmpty()) {
    QMessageBox::information(this, "New Folder", directory);
  }
}

void MainWindow::navigateTo(MainWindowPage page) {
  m_navigationController->navigateTo(page);
}

void MainWindow::navigateToListStatus(anime::list::Status status) {
  m_navigationController->navigateToListStatus(status);
}

void MainWindow::updateTitle() {
  auto title = u"Taiga"_s;

  if (taiga::app()->isDebug()) {
    title += u" [debug]"_s;
  }

  setWindowTitle(title);
}

void MainWindow::searchTorrents(const QString& title) {
  navigateTo(MainWindowPage::Torrents);
  initPage(MainWindowPage::Torrents);
  m_searchBox->setText(title);
  m_torrentsWidget->search(title);
}

void MainWindow::quit() {
  // Closing to the tray would otherwise swallow the close event that quitting sends.
  m_quitting = true;
  QApplication::quit();
}

void MainWindow::scanAvailableEpisodes() {
  if (track::availableEpisodes.isScanning()) return;
  if (taiga::settings.libraryFolders().empty()) {
    m_statusBarController->showMessage({
        .source = StatusBarController::Source::Library,
        .text = tr("Add a library folder in Settings to scan for episodes."),
        .spin = false,
    });
    return;
  }
  m_statusBarController->showMessage({
      .source = StatusBarController::Source::Library,
      .text = tr("Scanning library folders..."),
  });
  track::availableEpisodes.scan();
}

namespace {

// Watching entries whose next episode is in the library folders, most recently updated first.
QList<const ListEntry*> watchingWithNextEpisode() {
  QList<const ListEntry*> entries;
  for (const auto& entry : anime::db.entries()) {
    if (entry.status != anime::list::Status::Watching || entry.pending_delete) continue;
    if (!track::availableEpisodes.hasNext(entry.anime_id, entry.watched_episodes)) continue;
    entries.append(&entry);
  }
  std::ranges::sort(entries, [](const ListEntry* a, const ListEntry* b) {
    return a->last_updated > b->last_updated;
  });
  return entries;
}

}  // namespace

void MainWindow::playNextEpisode() {
  const auto entries = watchingWithNextEpisode();
  if (entries.isEmpty()) {
    m_statusBarController->showMessage({
        .source = StatusBarController::Source::Playback,
        .text = tr("No new episodes of anime you're watching were found in your library folders."),
        .spin = false,
    });
    return;
  }
  const auto* entry = entries.front();
  track::playEpisode(entry->anime_id, entry->watched_episodes + 1);
}

void MainWindow::playRandomAnime() {
  const auto entries = watchingWithNextEpisode();
  if (entries.isEmpty()) {
    playNextEpisode();  // shows why nothing can be played
    return;
  }
  const auto* entry = entries.at(QRandomGenerator::global()->bounded(entries.size()));
  track::playEpisode(entry->anime_id, entry->watched_episodes + 1);
}

void MainWindow::checkForUpdates(bool silent) {
  QNetworkRequest request{QUrl{u"https://api.github.com/repos/erengy/taiga/releases/latest"_s}};
  request.setHeaders(taiga::NetworkAccessManager::commonHeaders());  // GitHub needs a user agent

  if (!silent) {
    m_statusBarController->showMessage({
        .source = StatusBarController::Source::Update,
        .text = tr("Checking for updates..."),
    });
  }

  auto* reply = taiga::network()->get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply, silent] {
    const auto json = QJsonDocument::fromJson(reply->readAll()).object();
    const auto tag = json["tag_name"].toString();

    const auto showStatus = [this](const QString& text) {
      m_statusBarController->showMessage({
          .source = StatusBarController::Source::Update,
          .text = text,
          .spin = false,
      });
    };

    if (reply->error() != QNetworkReply::NoError || tag.isEmpty()) {
      if (!silent) showStatus(tr("Couldn't check for updates: %1").arg(reply->errorString()));
      return;
    }

    const semaver::Version latest{QString{tag}.remove(u'v').toStdString()};
    if (!(taiga::version() < latest)) {
      if (!silent) showStatus(tr("Taiga is up to date."));
      return;
    }

    m_statusBarController->clearMessage(StatusBarController::Source::Update);
    const auto answer = QMessageBox::question(
        this, tr("Update available"),
        tr("Taiga %1 is available. You have %2.\n\nOpen the download page?")
            .arg(QString::fromStdString(latest.to_string()))
            .arg(QString::fromStdString(taiga::version().to_string())));
    if (answer == QMessageBox::Yes) {
      QDesktopServices::openUrl(QUrl{json["html_url"].toString()});
    }
  });
}

void MainWindow::displayWindow() {
  show();
  setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
  activateWindow();
}

void MainWindow::about() {
  displayAboutDialog(this);
}

void MainWindow::donate() const {
  QDesktopServices::openUrl(QUrl("https://taiga.moe/#donate"));
}

void MainWindow::support() const {
  QDesktopServices::openUrl(QUrl("https://taiga.moe/#support"));
}

void MainWindow::synchronize() {
  setEnabled(false);

  const auto serviceName = sync::serviceName(sync::currentServiceId());
  const auto text = sync::willAuthenticate() ? tr("Authenticating with %1...").arg(serviceName)
                                             : tr("Synchronizing with %1...").arg(serviceName);

  m_statusBarController->showMessage({
      .source = StatusBarController::Source::Sync,
      .text = text,
  });

  if (!sync::synchronize()) {
    m_statusBarController->clearMessage(StatusBarController::Source::Sync);
    setEnabled(true);
  }
}

void MainWindow::profile() {
  navigateTo(MainWindowPage::Profile);
}

}  // namespace gui
