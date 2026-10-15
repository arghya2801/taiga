// Real SQLite, model, queue and widget checks. All data is temporary and HTTP is loopback-only.
#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStyleHints>
#include <QTabWidget>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeView>
#include <QUrlQuery>
#include <cstdio>
#ifdef Q_OS_WIN
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif
#include <functional>
#include <memory>

#include "gui/main/main_window.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/models/anime_list_model.hpp"
#include "gui/models/anime_list_proxy_model.hpp"
#include "gui/stats/stats_widget.hpp"
#include "gui/utils/image_provider.hpp"
#include "gui/utils/theme.hpp"
#include "media/anime_db.hpp"
#include "media/anime_history.hpp"
#include "media/manga_library.hpp"
#include "sync/myanimelist/myanimelist.hpp"
#include "sync/queue.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/application.hpp"
#include "taiga/options.hpp"
#include "taiga/settings.hpp"

namespace {
int failures = 0;
void check(bool ok, const char* label) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAILED: %s\n", label);
  }
}
bool waitFor(const std::function<bool()>& predicate, int timeout = 10000) {
  QElapsedTimer t;
  t.start();
  while (!predicate() && t.elapsed() < timeout) {
    QCoreApplication::processEvents();
    QThread::msleep(1);
  }
  return predicate();
}
class Server : public QTcpServer {
public:
  bool fail = false;
  QList<int> chapters;
  QString remoteStart;
  int remoteChapters = 2;
  Server() {
    listen(QHostAddress::LocalHost);
    connect(this, &QTcpServer::newConnection, this, [this] {
      while (hasPendingConnections()) {
        auto* socket = nextPendingConnection();
        auto bytes = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes] {
          *bytes += socket->readAll();
          const int end = bytes->indexOf("\r\n\r\n");
          if (end < 0) return;
          int length = 0;
          for (const auto& line : bytes->left(end).split('\n'))
            if (line.toLower().startsWith("content-length:"))
              length = line.mid(15).trimmed().toInt();
          if (bytes->size() < end + 4 + length) return;
          QJsonObject reply;
          const bool mutation = bytes->startsWith("PATCH");
          if (mutation) {
            QUrlQuery body(QString::fromUtf8(bytes->mid(end + 4, length)));
            const int count = body.queryItemValue("num_chapters_read").toInt();
            chapters.append(count);
            if (!fail) {
              remoteChapters = count;
              if (body.hasQueryItem("start_date")) remoteStart = body.queryItemValue("start_date");
            }
            reply = {{"status", "reading"},       {"num_chapters_read", count},
                     {"num_volumes_read", 1},     {"score", 8},
                     {"start_date", remoteStart}, {"updated_at", "2026-10-09T12:00:00+00:00"}};
          } else if (bytes->left(end).contains("mangalist")) {
            reply = {
                {"data",
                 QJsonArray{QJsonObject{
                     {"node", QJsonObject{{"id", 42},
                                          {"title", "Manga test"},
                                          {"num_chapters", 100},
                                          {"media_type", "manga"},
                                          {"mean", 7.0},
                                          {"genres", QJsonArray{QJsonObject{{"name", "Drama"}}}}}},
                     {"list_status", QJsonObject{{"status", "reading"},
                                                 {"score", 8},
                                                 {"num_chapters_read", remoteChapters}}}}}},
                {"paging", QJsonObject{}}};
          } else
            reply = {{"data", QJsonArray{}}, {"paging", QJsonObject{}}};
          const bool error = mutation && fail;
          const auto payload = QJsonDocument(error ? QJsonObject{{"error", "server_error"}} : reply)
                                   .toJson(QJsonDocument::Compact);
          socket->write((error ? QByteArray("HTTP/1.1 500 Internal Server Error\r\n")
                               : QByteArray("HTTP/1.1 200 OK\r\n")) +
                        "Content-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                        QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
          socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      }
    });
  }
};
}  // namespace

int main(int argc, char* argv[]) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  QElapsedTimer startup;
  startup.start();
  taiga::Application app(argc, argv);
  QTemporaryDir dir;
  check(dir.isValid(), "temporary test directory exists");
  app.setProperty("taiga.testDataPath", dir.path());
  Server server;
  app.setProperty("taiga.testApiBase",
                  QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
  QDir().mkpath(dir.path());
  taiga::settings.setService("myanimelist");
  taiga::settings.setSyncEnabled(false);
  taiga::accounts.setMyanimelistUsername("checks");
  taiga::accounts.setMyanimelistAccessToken("");
  taiga::opt::checkUpdatesOnStartup.set(false);
  taiga::opt::scanOnStartup.set(false);
  anime::db.init();
  anime::history.init();
  sync::queue.init();
  std::printf("Qt and empty-cache initialization: %.2f ms\n", startup.nsecsElapsed() / 1000000.0);
  check(QSqlDatabase::database().isOpen(),
        "history and queue leave the shared SQLite connection open");
  QList<Anime> items;
  QList<ListEntry> entries;
  const int count = app.arguments().contains("--benchmark") ? 10000 : 120;
  for (int id = 1; id <= count; ++id) {
    Anime a;
    a.id = id;
    a.titles.romaji = "Title " + std::to_string(id);
    a.type = anime::Type::Tv;
    a.status = anime::Status::FinishedAiring;
    a.episode_count = 12;
    a.episode_length = 24;
    a.score = 7;
    a.genres = id % 2 ? std::vector<std::string>{"Drama"} : std::vector<std::string>{"Action"};
    a.studios = {"Studio"};
    a.date_started = FuzzyDate("2024-04-01");
    items.append(a);
    ListEntry e;
    e.anime_id = id;
    e.watched_episodes = 12;
    e.score = 80;
    e.status = anime::list::Status::Completed;
    e.date_completed = FuzzyDate("2025-06-01");
    entries.append(e);
  }
  QElapsedTimer clock;
  clock.start();
  anime::db.updateItems(items);
  anime::db.updateEntries(entries);
  std::printf("SQLite batch: %d titles in %.2f ms\n", count, clock.nsecsElapsed() / 1000000.0);
  anime::history.add(1, 1, 1);
  sync::queue.push(1, anime::list::Field::Score);
  auto edited = entries[0];
  edited.score = 90;
  edited.last_updated = 10;
  anime::db.updateEntry(edited);
  QSqlQuery q;
  q.exec("SELECT score FROM anime_list WHERE media_id=1");
  check(q.next() && q.value(0).toInt() == 90,
        "list writes persist after history and queue operations");
  check(anime::db.entry(1) && anime::db.entry(1)->score == 90,
        "persisted edits update the in-memory list");
  auto remote = edited;
  remote.score = 10;
  remote.last_updated = 20;
  sync::applyFetchedEntries({remote});
  check(anime::db.entry(1)->score == 90, "remote sync preserves pending local edits");
  sync::queue.clear();
  remote.last_updated = 5;
  sync::applyFetchedEntries({remote});
  check(anime::db.entry(1)->score == 90, "older list responses cannot overwrite recent edits");
  gui::AnimeListModel model(nullptr);
  gui::AnimeListProxyModel proxy(nullptr);
  proxy.setSourceModel(&model);
  clock.restart();
  proxy.sort(gui::AnimeListModel::COLUMN_SCORE, Qt::DescendingOrder);
  std::printf("Sort: %d titles in %.2f ms\n", count, clock.nsecsElapsed() / 1000000.0);
  clock.restart();
  proxy.setTextFilter("genre:drama score:>=8");
  check(proxy.rowCount() == count / 2, "actual list model supports field queries");
  std::printf("Filter: %d titles in %.2f ms\n", count, clock.nsecsElapsed() / 1000000.0);

  proxy.setTextFilter("");
  auto renamed = *anime::db.item(3);
  renamed.titles.english = "Alpha";
  anime::db.updateItem(renamed);
  taiga::settings.setTitleLanguage(anime::TitleLanguage::English);
  proxy.sort(gui::AnimeListModel::COLUMN_TITLE, Qt::AscendingOrder);
  check(model.getAnime(proxy.mapToSource(proxy.index(0, 0)))->id == 3,
        "title sorting follows the selected language");
  renamed.titles.english = "Zzz";
  anime::db.updateItem(renamed);
  check(waitFor([&] { return model.getAnime(proxy.mapToSource(proxy.index(0, 0)))->id != 3; }),
        "title sort cache invalidates when metadata changes");
  taiga::settings.setTitleLanguage(anime::TitleLanguage::Romaji);

  manga::library.init();
  check(waitFor([] { return manga::library.ready(); }), "manga cache loads asynchronously");
  manga::Entry manga;
  manga.id = 42;
  manga.title = "Manga test";
  manga.status = "reading";
  manga.chapters = 100;
  manga.chaptersRead = 2;
  manga.score = 8;
  manga.onList = true;
  manga.genres = {"Drama"};
  manga.authors = {"Author"};
  sync::myanimelist::Service::instance()->mangaListFetched({manga});
  manga.chaptersRead = 3;
  manga.startedReading = "2025-00-00";
  manga.startDate = "2024";
  check(manga::library.save(manga), "offline manga edits are accepted after durable queue write");
  check(manga::library.pendingCount() == 1, "offline manga edits remain queued");
  {
    manga::Library recovered;
    recovered.init();
    check(waitFor([&] { return recovered.ready(); }), "pending manga queue reloads");
    check(recovered.pendingCount() == 1 && recovered.entries().value(42).chaptersRead == 3,
          "pending manga progress survives library restart");
  }
  sync::queue.clear();
  taiga::accounts.setMyanimelistAccessToken("test-token");
  taiga::settings.setSyncEnabled(true);
  bool failed = false;
  QObject::connect(sync::myanimelist::Service::instance(),
                   &sync::myanimelist::Service::mangaMutationFailed, &app,
                   [&](int) { failed = true; });
  server.fail = true;
  manga::library.refresh();
  check(waitFor([&] { return failed; }), "transient manga HTTP failure is reported");
  check(manga::library.pendingCount() == 1, "failed manga edits remain durable");
  server.fail = false;
  manga::library.refresh();
  check(waitFor([] { return manga::library.pendingCount() == 0; }),
        "manga retry succeeds and clears the queue");
  check(manga::library.entries().value(42).updatedAt == "2026-10-09T12:00:00+00:00",
        "manga update reconciles server timestamp");
  check(manga::library.entries().value(42).startedReading == "2025-00-00",
        "manga reading dates survive the update response");
  manga = manga::library.entries().value(42);
  manga.chaptersRead = 4;
  manga::library.save(manga);
  manga.chaptersRead = 5;
  manga::library.save(manga);
  check(waitFor([] { return manga::library.pendingCount() == 0; }),
        "edits during an in-flight manga request sync in order");
  check(manga::library.entries().value(42).chaptersRead == 5,
        "older responses cannot overwrite newer manga edits");
  taiga::settings.setSyncEnabled(false);

  // A varied account catches layout and chart bugs that identical completed titles cannot.
  const QStringList names{"Fullmetal Alchemist: Brotherhood",
                          "Cowboy Bebop",
                          "Frieren: Beyond Journey’s End",
                          "Mob Psycho 100",
                          "A Silent Voice",
                          "Steins;Gate",
                          "Haikyu!!",
                          "Violet Evergarden",
                          "March Comes in Like a Lion",
                          "Made in Abyss",
                          "Kaguya-sama: Love Is War",
                          "Vinland Saga",
                          "Odd Taxi",
                          "Bocchi the Rock!",
                          "Samurai Champloo",
                          "Spirited Away",
                          "Monster",
                          "Ping Pong the Animation"};
  const QStringList studios{"Madhouse", "Kyoto Animation", "Bones",
                            "MAPPA",    "Production I.G",  "Sunrise"};
  const QStringList genres{"Fantasy", "Comedy", "Romance", "Sci-Fi", "Adventure", "Slice of Life"};
  for (int i = 0; i < count; ++i) {
    auto& a = items[i];
    auto& e = entries[i];
    const int id = i + 1;
    a.titles.romaji =
        (names[i % names.size()] + (i >= names.size() ? QString(" #%1").arg(id) : QString{}))
            .toStdString();
    a.type = id % 9 == 0 ? anime::Type::Movie : id % 7 == 0 ? anime::Type::Ona : anime::Type::Tv;
    a.episode_count = id % 9 == 0 ? 1 : id % 11 == 0 ? 64 : id % 3 == 0 ? 24 : 12;
    a.episode_length = id % 9 == 0 ? 108 : 24;
    a.score = 6.1f + (id % 26) / 10.0f;
    a.genres.push_back(genres[id % genres.size()].toStdString());
    a.studios = {studios[id % studios.size()].toStdString()};
    a.date_started = FuzzyDate(
        QString("%1-%2-01").arg(1990 + id % 37).arg(1 + id % 12, 2, 10, QChar('0')).toStdString());
    switch (id % 10) {
      case 0:
      case 1:
        e.status = anime::list::Status::PlanToWatch;
        break;
      case 2:
        e.status = anime::list::Status::Watching;
        break;
      case 3:
        e.status = anime::list::Status::OnHold;
        break;
      case 4:
        e.status = anime::list::Status::Dropped;
        break;
      default:
        e.status = anime::list::Status::Completed;
        break;
    }
    e.score = id % 4 == 0 || e.status == anime::list::Status::PlanToWatch ? 0 : (5 + id % 6) * 10;
    e.watched_episodes = e.status == anime::list::Status::Completed ? a.episode_count
                         : e.status == anime::list::Status::PlanToWatch
                             ? 0
                             : std::min(a.episode_count, 1 + id % 8);
    e.rewatched_times = e.status == anime::list::Status::Completed && id % 7 == 0 ? 1 + id % 3 : 0;
    e.last_updated =
        QDateTime(QDate(2025, 1, 1).addDays(id % 550), QTime(12, 0)).toSecsSinceEpoch();
    e.date_completed = e.status == anime::list::Status::Completed && id % 17 != 0
                           ? FuzzyDate(QString("%1-%2-%3")
                                           .arg(2024 + id % 3)
                                           .arg(1 + id % 12, 2, 10, QChar('0'))
                                           .arg(1 + id % 28, 2, 10, QChar('0'))
                                           .toStdString())
                           : FuzzyDate{};
  }
  anime::db.updateItems(items);
  anime::db.updateEntries(entries);
  gui::theme.initStyle();
  gui::imageProvider.init();
  gui::StatsWidget widget(nullptr);
  widget.setAttribute(Qt::WA_DontShowOnScreen);
  widget.resize(1120, 850);
  widget.show();
  auto* titles = widget.findChild<QTableView*>("statsTitles");
  auto* groups = widget.findChild<QListView*>("statsGroups");
  auto* tabs = widget.findChild<QTabWidget*>("statsTabs");
  auto* histogram = widget.findChild<QListView*>("statsChart1");
  check(titles && groups && histogram && tabs && tabs->count() == 5,
        "statistics have navigable chart pages and a virtual title drilldown");
  check(waitFor([&] { return titles->isEnabled() && titles->model()->rowCount() == count; }),
        "statistics render the full cached anime list");
  auto* genre = widget.findChild<QComboBox*>("statsGenre");
  genre->setCurrentIndex(genre->findData("Drama"));
  check(waitFor([&] { return titles->isEnabled() && titles->model()->rowCount() == count / 2; }),
        "statistics genre filter updates asynchronously");
  histogram->setCurrentIndex(histogram->model()->index(7, 0));
  QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(histogram, &space);
  int ratedEight = 0;
  for (const auto& e : entries) ratedEight += e.anime_id % 2 == 1 && e.score == 80;
  check(tabs->currentIndex() == 4 && titles->model()->rowCount() == ratedEight,
        "keyboard activation drills down to the score bar’s actual title IDs");
  widget.findChild<QPushButton*>("statsBackToChart")->click();
  check(tabs->currentIndex() == 0, "back from a drilldown restores its chart page");
  tabs->setCurrentIndex(0);
  histogram->setCurrentIndex(histogram->model()->index(2, 0));
  QCoreApplication::sendEvent(histogram, &space);
  check(titles->model()->rowCount() == 0,
        "an empty chart bucket shows zero titles rather than the whole list");
  widget.findChild<QPushButton*>("statsAllTitles")->click();
  check(titles->model()->rowCount() == count / 2,
        "clear drilldown restores the globally filtered titles");
  auto* kind = widget.findChild<QComboBox*>("statsKind");
  kind->setCurrentIndex(1);
  check(waitFor([&] { return titles->isEnabled() && titles->model()->rowCount() == 1; }),
        "manga statistics use the shared account cache");
  auto* ranking = widget.findChild<QComboBox*>("statsRanking");
  check(!ranking->model()->index(3, 0).flags().testFlag(Qt::ItemIsEnabled),
        "manga has no invented watch-time ranking");
  kind->setCurrentIndex(0);
  genre->setCurrentIndex(0);
  check(waitFor([&] { return titles->isEnabled() && titles->model()->rowCount() == count; }),
        "switching back restores complete anime statistics");
  auto* months = widget.findChild<QListView*>("statsChart12");
  check(months->model()->rowCount() == 12,
        "completion timeline includes empty months in the selected year");
  auto* dimension = widget.findChild<QComboBox*>("statsDimension");
  dimension->setCurrentIndex(dimension->findData(int(stats::Dimension::Studio)));
  ranking->setCurrentIndex(1);
  check(groups->model()->rowCount() > 0, "studio mean ranking has sufficient rated samples");
  auto* minimum = widget.findChild<QSpinBox*>("statsMinimum");
  minimum->setValue(count + 1);
  check(groups->model()->rowCount() == 0, "mean rankings enforce the minimum rated sample");
  minimum->setValue(5);
  ranking->setCurrentIndex(0);
  auto* search = widget.findChild<QLineEdit*>("statsTitleSearch");
  search->setText("Fullmetal");
  int matching = 0;
  for (const auto& a : items)
    matching += QString::fromStdString(a.titles.romaji).contains("Fullmetal");
  check(titles->model()->rowCount() == matching,
        "drilldown supports title search without rebuilding account statistics");
  search->clear();
  tabs->setCurrentIndex(0);
  clock.restart();
  for (int i = 0; i < 30; ++i) {
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    widget.QWidget::render(&image);
  }
  std::printf("Statistics overview render: %.2f ms/frame\n", clock.nsecsElapsed() / 30000000.0);
  if (app.arguments().contains("--screenshot")) {
    const auto base = QCoreApplication::applicationDirPath();
    for (const auto& [themeName, scheme] : QList<QPair<QString, Qt::ColorScheme>>{
             {"dark", Qt::ColorScheme::Dark}, {"light", Qt::ColorScheme::Light}}) {
      app.styleHints()->setColorScheme(scheme);
      QCoreApplication::processEvents();
      for (int tab = 0; tab < 4; ++tab) {
        tabs->setCurrentIndex(tab);
        QCoreApplication::processEvents();
        widget.grab().save(base + QString("/statistics-%1-%2.png").arg(themeName).arg(tab));
      }
    }
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    tabs->setCurrentIndex(0);
    widget.resize(760, 720);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QCoreApplication::processEvents();
    widget.grab().save(base + "/statistics-narrow.png");
    auto* area = qobject_cast<QScrollArea*>(tabs->widget(0));
    area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
    QCoreApplication::processEvents();
    widget.grab().save(base + "/statistics-backlog.png");
  }
#ifdef Q_OS_WIN
  PROCESS_MEMORY_COUNTERS memory{};
  if (app.arguments().contains("--benchmark") &&
      GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
    std::printf("Peak working set: %.2f MiB\n", memory.PeakWorkingSetSize / 1048576.0);
#endif
  widget.hide();
  app.initWindowForChecks();
  auto* main = app.mainWindow();
  main->setAttribute(Qt::WA_DontShowOnScreen);
  main->resize(1180, 850);
  main->show();
  main->navigateTo(gui::MainWindowPage::Stats);
  auto* mainStats = main->findChild<gui::StatsWidget*>();
  auto* mainTitles = mainStats ? mainStats->findChild<QTableView*>("statsTitles") : nullptr;
  check(mainTitles && waitFor([&] {
          return mainTitles->isEnabled() && mainTitles->model()->rowCount() == count;
        }),
        "statistics load from the real main-window sidebar with the cached account");
  for (int i = 0; i < 3; ++i) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QCoreApplication::processEvents();
  }
  if (mainStats) {
    auto* statusChart = mainStats->findChild<QListView*>("statsChart0");
    check(statusChart &&
              statusChart->y() + statusChart->height() <= statusChart->parentWidget()->height(),
          "main-window cards leave room for every status bar after layout settles");
  }
  if (app.arguments().contains("--screenshot"))
    main->grab().save(QCoreApplication::applicationDirPath() + "/statistics-main-window.png");
  main->navigateTo(gui::MainWindowPage::Manga);
  auto* mangaWidget = main->findChild<QWidget*>("mangaWidget");
  auto* mangaView = mangaWidget ? mangaWidget->findChild<QTreeView*>("animeList") : nullptr;
  check(mangaView && waitFor([&] { return mangaView->model()->rowCount() == 1; }),
        "main manga page uses the shared cached list");
  if (mangaView) {
    auto* source = qobject_cast<QSortFilterProxyModel*>(mangaView->model())->sourceModel();
    int removed = 0, resets = 0;
    QObject observer;
    QObject::connect(source, &QAbstractItemModel::rowsRemoved, &observer, [&] { ++removed; });
    QObject::connect(source, &QAbstractItemModel::modelReset, &observer, [&] { ++resets; });
    manga = manga::library.entries().value(42);
    manga.chaptersRead = 6;
    manga::library.save(manga);
    check(source->rowCount() == 1 && source->index(0, 1).data(Qt::UserRole).toInt() == 6 &&
              removed == 0 && resets == 0,
          "editing manga updates its row without clearing the table");
  }
  taiga::accounts.setMyanimelistAuthenticated(true);
  auto* syncAction = main->findChild<QAction*>("actionSynchronize");
  if (syncAction) {
    syncAction->trigger();
    check(main->isEnabled() && !syncAction->isEnabled(),
          "sync disables only its action and leaves the app usable");
    check(waitFor([&] { return syncAction->isEnabled(); }),
          "sync action returns when the list request completes");
  } else
    check(false, "main synchronize action exists");

  std::printf(failures ? "%d integration check(s) failed.\n" : "All integration checks passed.\n",
              failures);
  return failures;
}
