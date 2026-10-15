// Real SQLite, model, queue and widget checks. All data is temporary and HTTP is loopback-only.
#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
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
#include <QThreadPool>
#include <QToolButton>
#include <QTreeView>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <cstdio>
#ifdef Q_OS_WIN
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif
#include <QAbstractItemView>
#include <QBuffer>
#include <QPixmapCache>
#include <functional>
#include <memory>

#include "compat/anime.hpp"
#include "compat/settings.hpp"
#include "gui/common/anime_list_view.hpp"
#include "gui/common/anime_list_view_base.hpp"
#include "gui/list/list_widget.hpp"
#include "gui/main/main_window.hpp"
#include "gui/main/navigation_widget.hpp"
#include "gui/models/anime_list_model.hpp"
#include "gui/models/anime_list_proxy_model.hpp"
#include "gui/search/search_widget.hpp"
#include "gui/stats/stats_widget.hpp"
#include "gui/utils/image_provider.hpp"
#include "gui/utils/theme.hpp"
#include "media/account_activity.hpp"
#include "media/anime_db.hpp"
#include "media/anime_history.hpp"
#include "media/anime_list_export.hpp"
#include "media/anime_list_utils.hpp"
#include "media/manga_library.hpp"
#include "sync/myanimelist/myanimelist.hpp"
#include "sync/queue.hpp"
#include "sync/service.hpp"
#include "taiga/accounts.hpp"
#include "taiga/application.hpp"
#include "taiga/options.hpp"
#include "taiga/session.hpp"
#include "taiga/settings.hpp"
#include "track/feed_filter.hpp"

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

void checkMigration(const QString& root) {
  const auto path = root + "/legacy-settings.xml";
  const auto write = [](const QString& path, const QByteArray& data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
  };
  const QByteArray xml = R"(<settings>
<announce><discord enabled="true" applicationid="1234" groupenabled="false" timeenabled="true" usernameenabled="false"/>
<http enabled="true" url="https://example.invalid/announce" format="title=%title%&amp;ep=%episode%"/></announce>
<rss><torrent><source address="https://example.invalid/rss?x=1&amp;y=2"/><search address="https://example.invalid/rss?q=%title%"/>
<options autocheck="true" checkinterval="99999" newaction="2"/><application mode="2" path="C:/test/torrent.exe"/>
<filter enabled="true"><item name="Preferred release" action="prefer" match="all" option="default" enabled="true">
<anime id="42"/><condition element="episode_group" operator="contains" value="Subs"/>
<condition element="meta_date_start" operator="ge" value="2020-01-01"/></item>
<item name="Unsupported" action="select" match="all" option="default" enabled="true"><condition element="unknown" operator="equals" value="x"/></item>
</filter></torrent></rss><program><general enablesharing="false" enablesync="false"/><list><action doubleclick="2" middleclick="0" titlelang="romaji"/></list></program></settings>)";
  check(write(path, xml), "legacy settings fixture writes");
  taiga::opt::httpUrl.set("https://keep.invalid");
  auto incremental = xml;
  incremental.replace("</settings>",
                      "<account><myanimelist username=\"must-not-replace\"/></account></settings>");
  check(write(path, incremental), "incremental migration fixture writes");
  check(compat::v1::readSettings(path.toStdString(), taiga::settings, taiga::accounts, true) &&
            taiga::opt::httpUrl.get() == "https://keep.invalid" &&
            taiga::opt::discordApplicationId.get() == "1234" &&
            taiga::accounts.myanimelistUsername() == "checks",
        "incremental migration fills missing preferences while preserving explicit values and "
        "accounts");
  check(write(path, xml), "complete migration fixture restores");
  compat::v1::readSettings(path.toStdString(), taiga::settings, taiga::accounts);
  check(taiga::opt::discordEnabled.get() && !taiga::opt::discordShowGroup.get() &&
            !taiga::opt::discordShowUsername.get() &&
            taiga::opt::discordApplicationId.get() == "1234" && !taiga::opt::sharingEnabled.get(),
        "v1 sharing preferences migrate without dropping privacy choices");
  check(taiga::opt::httpUrl.get() == "https://example.invalid/announce" &&
            taiga::opt::httpFormat.get() == "title=%title%&ep=%episode%",
        "v1 HTTP sharing format survives XML decoding");
  check(taiga::opt::torrentSource.get().endsWith("x=1&y=2") &&
            taiga::opt::torrentSearchSource.get().contains("%title%") &&
            taiga::opt::torrentAutoCheckMinutes.get() == 1440 &&
            taiga::opt::torrentNewAction.get() == int(taiga::opt::TorrentNewAction::Download) &&
            taiga::opt::torrentApp.get() == "C:/test/torrent.exe",
        "RSS sources, limits and legacy action numbers migrate correctly");
  const auto filters = track::feed::fromJson(
      QJsonDocument::fromJson(taiga::opt::torrentFilters.get().toUtf8()).array());
  check(filters.size() == 2 && filters[0].enabled && filters[0].animeIds == QList<int>{42} &&
            filters[0].conditions.size() == 2 && !filters[1].enabled,
        "v1 filter shortcodes migrate and unsupported rules stay disabled");
  track::feed::Item item;
  item.animeId = 42;
  item.group = "GoodSubs";
  track::feed::Context context;
  context.dateStarted = "2024-01-01";
  check(filters[0].matches(item, context),
        "migrated release-date and group conditions evaluate correctly");
  check(write(path, "<settings><announce><http url=\"https://wrong.invalid\"/></announce>"),
        "truncated settings fixture writes");
  compat::v1::readSettings(path.toStdString(), taiga::settings, taiga::accounts);
  check(taiga::opt::httpUrl.get() == "https://example.invalid/announce",
        "malformed migration cannot partially replace existing settings");
  check(taiga::opt::listDoubleClick.get() == int(taiga::opt::ListAction::OpenFolder) &&
            taiga::opt::listMiddleClick.get() == int(taiga::opt::ListAction::Nothing) &&
            !taiga::settings.syncEnabled(),
        "legacy mouse action enums and disabled sync preferences migrate correctly");
  QDir().mkpath(root + "/v1");
  check(write(root + "/v1/settings.xml", xml), "incremental startup migration fixture saves");
  taiga::opt::httpUrl.set("https://keep-current.invalid");
  taiga::settings.init();
  check(taiga::settings.hasOption("meta.v1SettingsVersion") &&
            taiga::opt::httpUrl.get() == "https://keep-current.invalid",
        "startup marks incremental migration without overwriting current settings");
  taiga::opt::listDoubleClick.set(int(taiga::opt::ListAction::ViewDetails));
  taiga::opt::listMiddleClick.set(int(taiga::opt::ListAction::PlayNextEpisode));
  taiga::opt::sharingEnabled.set(false);
  taiga::opt::discordEnabled.set(false);
  taiga::opt::httpEnabled.set(false);
  taiga::opt::torrentAutoCheck.set(false);
  taiga::opt::torrentApp.set("");
  taiga::opt::torrentFilters.set("");
  const auto dbPath = root + "/legacy-anime.xml";
  check(
      write(
          dbPath,
          R"(<database><anime><id name="myanimelist">42</id><id name="anilist">99</id><title>MAL title</title></anime>
<anime><id name="anilist">88</id><title>Unmapped</title></anime><anime><id>77</id><title>Legacy</title></anime></database>)"),
      "v1 multi-service ID fixture writes");
  const auto mal = compat::v1::readAnimeDatabase(dbPath.toStdString(), "myanimelist");
  const auto anilist = compat::v1::readAnimeDatabase(dbPath.toStdString(), "anilist");
  check(mal.size() == 2 && mal[0].id == 42 && mal[1].id == 77 && anilist[0].id == 99,
        "v1 migration selects the requested service ID and excludes unmapped metadata");
}

void checkActivityPersistence() {
  const auto baseline = *anime::db.entry(1);
  const auto before = stats::activity.records(stats::MediaKind::Anime).size();
  auto updated = baseline;
  updated.score = 70;
  anime::list::save(updated);
  updated.watched_episodes = 11;
  anime::list::save(updated);
  updated.watched_episodes = 12;
  anime::list::save(updated);
  anime::list::save(updated);
  check(stats::activity.records(stats::MediaKind::Anime).size() == before + 3,
        "local progress and score edits record once, including corrections");
  stats::ActivityLog recovered;
  check(recovered.records(stats::MediaKind::Anime).size() == before + 3,
        "activity survives loading a fresh log instance from SQLite");
  taiga::accounts.setMyanimelistUsername("other-check-account");
  check(!manga::library.save(manga::library.entries().value(42)) && !manga::library.remove(42),
        "an account switch cannot edit the previous account's manga cache or history");
  check(recovered.records(stats::MediaKind::Anime).isEmpty(),
        "activity is isolated by MAL account");
  taiga::accounts.setMyanimelistUsername("checks");
  check(recovered.records(stats::MediaKind::Anime).size() == before + 3,
        "switching back restores the original account history");
  QSqlQuery query;
  check(query.exec("CREATE TEMP TRIGGER reject_check BEFORE INSERT ON anime_list WHEN "
                   "NEW.media_id=1 AND (NEW.score=81 OR NEW.pending_delete=1) "
                   "BEGIN SELECT RAISE(FAIL,'test failure'); END"),
        "write failure fixture installs");
  auto rejected = baseline;
  rejected.score = 81;
  auto other = *anime::db.entry(2);
  const int otherScore = other.score;
  other.score = 75;
  check(!anime::db.updateEntries({other, rejected}) && anime::db.entry(2)->score == otherScore,
        "failed list batch rolls back without publishing partially written cache values");
  anime::list::save(rejected);
  check(stats::activity.records(stats::MediaKind::Anime).size() == before + 3 &&
            anime::db.entry(1)->score == 70,
        "failed list writes cannot create fabricated activity or change cached entries");
  sync::queue.clear();
  anime::list::remove(1);
  check(!anime::db.entry(1)->pending_delete && !sync::queue.hasItem(1),
        "a failed local removal cannot queue a remote deletion");
  query.exec("DROP TRIGGER reject_check");
  anime::db.updateEntry(baseline);
  sync::queue.clear();
}

class Server : public QTcpServer {
public:
  bool fail = false;
  int seasonRequests = 0;
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
          if (bytes->left(end).contains("/anime/season/")) ++seasonRequests;
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
  qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
    if (type != QtDebugMsg && type != QtInfoMsg)
      std::fprintf(stderr, "Qt: %s\n", message.toUtf8().constData());
  });
  QElapsedTimer startup;
  startup.start();
  taiga::Application app(argc, argv);
  if (app.arguments().contains("--startup-probe")) {
    const int index = app.arguments().indexOf("--startup-probe");
    const auto path = app.arguments().value(index + 1);
    if (path.isEmpty() || !QFile::exists(path + "/media.sqlite")) return 2;
    app.setProperty("taiga.testDataPath", path);
    app.setProperty("taiga.testStartupProbe", true);
    taiga::settings.setSyncEnabled(false);
    taiga::settings.setDetectionEnabled(false);
    taiga::opt::checkUpdatesOnStartup.set(false);
    taiga::opt::scanOnStartup.set(false);
    taiga::opt::torrentAutoCheck.set(false);
    taiga::opt::sharingEnabled.set(false);
    return app.run();
  }
  QTemporaryDir dir;
  check(dir.isValid(), "temporary test directory exists");
  if (!dir.isValid()) return failures;
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
  checkMigration(dir.path());
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
  check(model.rowCount(model.index(0, 0)) == 0, "flat anime model has no child rows");
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

  checkActivityPersistence();

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
  check(titles && groups && histogram && tabs && tabs->count() == 6,
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
  check(tabs->currentIndex() == 5 && titles->model()->rowCount() == ratedEight,
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
      for (int tab = 0; tab < 5; ++tab) {
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
  std::printf("Checking main window...\n");
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

  std::printf("Checking grouped workflows...\n");
  main->navigateTo(gui::MainWindowPage::List);
  auto* listPage = main->findChild<gui::ListWidget*>();
  check(listPage != nullptr, "main anime list page exists");
  if (listPage) {
    for (auto mode :
         {gui::ListViewMode::Cards, gui::ListViewMode::List, gui::ListViewMode::Grouped}) {
      if (auto* previous = listPage->findChild<QAbstractItemView*>())
        if (previous->model()->rowCount())
          previous->setCurrentIndex(previous->model()->index(0, 0));
      listPage->setViewMode(mode);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    check(listPage->findChildren<gui::ListViewBase*>().size() == 1,
          "changing list modes destroys old search handlers along with their views");
    auto* view = listPage->findChild<gui::ListView*>();
    auto* grouped = qobject_cast<gui::AnimeListProxyModel*>(view->model());
    grouped->setListStatusFilter({.status = 0, .anyStatus = true});
    grouped->setTextFilter("");
    grouped->sort(gui::AnimeListModel::COLUMN_SCORE, Qt::DescendingOrder);
    int headings = 0;
    for (int row = 0; row < grouped->rowCount(); ++row)
      headings += !grouped->index(row, 0)
                       .data(int(gui::AnimeListItemDataRole::GroupHeading))
                       .toString()
                       .isEmpty();
    check(headings == 5 && grouped->rowCount() == count,
          "grouped list retains every title with one heading per list status");
    check(grouped->index(0, 0).data(int(gui::AnimeListItemDataRole::GroupHeading)).toString() ==
              "Watching",
          "group order stays stable during descending score sorts");
    std::printf("Checking group heading interaction...\n");
    view->scrollTo(grouped->index(0, 0), QAbstractItemView::PositionAtTop);
    QCoreApplication::processEvents();
    int doubleClicks = 0;
    QObject mouseObserver;
    QObject::connect(view, &QAbstractItemView::doubleClicked, &mouseObserver,
                     [&](const QModelIndex&) { ++doubleClicks; });
    const auto point = view->visualRect(grouped->index(0, 0)).topLeft() + QPoint(8, 5);
    QMouseEvent headingClick(QEvent::MouseButtonDblClick, QPointF(point),
                             QPointF(view->viewport()->mapToGlobal(point)), Qt::LeftButton,
                             Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view->viewport(), &headingClick);
    check(doubleClicks == 0, "group headings cannot activate the title below them");
    std::printf("Checking list keyboard actions...\n");
    view->setCurrentIndex(grouped->index(0, 0));
    auto* entry = grouped->index(0, 0)
                      .data(int(gui::AnimeListItemDataRole::ListEntry))
                      .value<const ListEntry*>();
    const int id = entry->anime_id, before = entry->watched_episodes;
    QKeyEvent increment(QEvent::KeyPress, Qt::Key_Plus, Qt::ControlModifier);
    QCoreApplication::sendEvent(view, &increment);
    check(anime::db.entry(id)->watched_episodes == before + 1,
          "Ctrl+plus increments the current episode through the normal save and queue path");
    QKeyEvent decrement(QEvent::KeyPress, Qt::Key_Minus, Qt::ControlModifier);
    QCoreApplication::sendEvent(view, &decrement);
    check(anime::db.entry(id)->watched_episodes == before, "Ctrl+minus corrects current progress");
    QKeyEvent score(QEvent::KeyPress, Qt::Key_8, Qt::ControlModifier);
    QCoreApplication::sendEvent(view, &score);
    check(anime::db.entry(id)->score == 80,
          "Ctrl+digit edits the personal score in the 0-100 internal scale");
    const auto oldAction = taiga::opt::listDoubleClick.get();
    taiga::opt::listDoubleClick.set(int(taiga::opt::ListAction::Nothing));
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(view, &enter);
    check(enter.isAccepted(), "Enter follows the configured action, including Nothing");
    taiga::opt::listDoubleClick.set(oldAction);
    main->searchBox()->setText("genre:drama");
    check(grouped->rowCount() == count / 2,
          "grouped lists keep advanced search working after repeated view changes");
    main->searchBox()->clear();
    grouped->setGrouping(gui::AnimeListGrouping::Type);
    headings = 0;
    for (int row = 0; row < grouped->rowCount(); ++row)
      headings += !grouped->index(row, 0)
                       .data(int(gui::AnimeListItemDataRole::GroupHeading))
                       .toString()
                       .isEmpty();
    check(headings == 3, "season-style groups preserve TV, movie and ONA titles");
    if (app.arguments().contains("--screenshot")) {
      view->scrollToTop();
      QCoreApplication::processEvents();
      main->grab().save(QCoreApplication::applicationDirPath() + "/grouped-list-check.png");
    }
    check(waitFor([&] { return stats::activity.records(stats::MediaKind::Anime).size() >= 5; }),
          "keyboard episode and score edits populate the persistent activity log");
    main->navigateTo(gui::MainWindowPage::Stats);
    auto* historyView = mainStats->findChild<QTableView*>("statsActivity");
    check(historyView && waitFor([&] {
            return mainTitles->isEnabled() && historyView->model()->rowCount() >= 5;
          }),
          "statistics refresh their activity history after local edits");
  }

  const auto exportPath = dir.path() + "/backup.xml";
  check(anime::list::exportAsXml(exportPath.toStdString()),
        "MAL anime XML export commits successfully");
  QFile exported(exportPath);
  check(exported.open(QIODevice::ReadOnly), "exported backup opens");
  const auto originalExport = exported.readAll();
  exported.close();
  QXmlStreamReader exportedXml(originalExport);
  int exportedTitles = 0;
  while (!exportedXml.atEnd()) {
    exportedXml.readNext();
    exportedTitles += exportedXml.isStartElement() && exportedXml.name() == u"anime";
  }
  check(!exportedXml.hasError() && exportedTitles == count,
        "MAL backup is valid XML and contains the complete active list");
  check(anime::list::exportAsMarkdown((dir.path() + "/backup.md").toStdString()),
        "Markdown backup commits successfully");
  taiga::settings.setService("anilist");
  check(!anime::list::exportAsXml(exportPath.toStdString()),
        "non-MAL numeric IDs cannot overwrite a MAL backup");
  taiga::settings.setService("myanimelist");
  check(exported.open(QIODevice::ReadOnly) && exported.readAll() == originalExport,
        "refused cross-service export leaves the previous backup untouched");
  exported.close();
  auto missing = ListEntry{.anime_id = count + 1, .status = anime::list::Status::PlanToWatch};
  anime::db.updateEntry(missing);
  auto pending = *anime::db.entry(count);
  auto removed = pending;
  removed.pending_delete = true;
  anime::db.updateEntry(removed);
  const auto incompletePath = dir.path() + "/backup-incomplete.xml";
  check(anime::list::exportAsXml(incompletePath.toStdString()),
        "offline backup tolerates missing metadata and pending removals");
  QFile incomplete(incompletePath);
  check(incomplete.open(QIODevice::ReadOnly), "incomplete-metadata backup opens");
  const auto incompleteXml = incomplete.readAll();
  incomplete.close();
  check(
      incompleteXml.contains(QByteArray("<series_animedb_id>") + QByteArray::number(count + 1) +
                             "</series_animedb_id>") &&
          !incompleteXml.contains(QByteArray("<series_animedb_id>") + QByteArray::number(count) +
                                  "</series_animedb_id>") &&
          !incompleteXml.contains("<series_episodes>-1</series_episodes>"),
      "backup preserves missing title IDs, excludes removals and keeps unknown totals nonnegative");
  anime::db.deleteEntry(count + 1);
  anime::db.updateEntry(pending);

  main->navigateTo(gui::MainWindowPage::Search);
  auto* searchPage = main->findChild<gui::SearchWidget*>();
  QToolButton* seasons = nullptr;
  if (searchPage)
    for (auto* button : searchPage->findChildren<QToolButton*>())
      if (button->text() == "Seasons") seasons = button;
  check(seasons && seasons->menu(), "season browser exposes its shortcut menu");
  if (seasons && seasons->menu()) {
    auto* menu = seasons->menu();
    QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection);
    const int requests = server.seasonRequests;
    check(menu->actions().size() == 3, "season menu contains previous, current and next");
    menu->actions()[1]->trigger();
    check(waitFor([&] { return server.seasonRequests == requests + 1; }),
          "season shortcut makes one request rather than one for each filter");
    auto* seasonView = searchPage->findChild<gui::ListView*>();
    auto* seasonProxy =
        seasonView ? qobject_cast<gui::AnimeListProxyModel*>(seasonView->model()) : nullptr;
    check(seasonProxy && !seasonProxy->filters().type.has_value() &&
              seasonProxy->filters().year == QDate::currentDate().year(),
          "season shortcut switches to grouped results across all formats");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

  if (app.arguments().contains("--benchmark") && listPage) {
    QList<Anime> ordered = items;
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
      return QString::fromStdString(a.titles.romaji)
                 .compare(QString::fromStdString(b.titles.romaji), Qt::CaseInsensitive) < 0;
    });
    QSet<int> posterIds;
    QList<int> sampleIds;
    int watchingPosters = 0;
    for (int i = 0; i < ordered.size(); ++i) {
      const int id = ordered[i].id;
      if (i < 200) posterIds.insert(id);
      if (i < 24) sampleIds.append(id);
      if (anime::db.entry(id)->status == anime::list::Status::Watching && watchingPosters++ < 100)
        posterIds.insert(id);
    }
    QImage poster(400, 560, QImage::Format_RGB32);
    for (int y = 0; y < poster.height(); ++y)
      for (int x = 0; x < poster.width(); ++x)
        poster.setPixelColor(x, y,
                             QColor::fromHsv((x / 3 + y / 4) % 360, 100 + (x % 80), 100 + y % 155));
    QByteArray imageBytes;
    QBuffer imageBuffer(&imageBytes);
    imageBuffer.open(QIODevice::WriteOnly);
    check(poster.save(&imageBuffer, "JPG", 85), "cached poster benchmark image encodes");
    const auto cache = dir.path() + "/cache/myanimelist/media";
    QDir().mkpath(cache);
    for (const int id : posterIds) {
      QFile file(cache + QString("/%1.jpg").arg(id));
      check(file.open(QIODevice::WriteOnly) && file.write(imageBytes) == imageBytes.size(),
            "cached poster fixture saves");
    }
    auto covered = items;
    for (auto& item : covered) item.image_url = "http://127.0.0.1:9/poster.jpg";
    anime::db.updateItems(covered);
    taiga::session.setAnimeListViewMode(gui::ListViewMode::Cards);
    taiga::session.setAnimeListSortColumn(gui::AnimeListModel::COLUMN_TITLE);
    taiga::session.setAnimeListSortOrder(Qt::AscendingOrder);
    for (int run = 0; run < 2; ++run) {
      QProcess child;
      child.setProgram(app.applicationFilePath());
      child.setArguments({"--startup-probe", dir.path()});
      QElapsedTimer elapsed;
      elapsed.start();
      child.start();
      QByteArray output;
      const bool ready = waitFor(
          [&] {
            output += child.readAllStandardOutput();
            return output.contains("FIRST_FRAME") || child.state() == QProcess::NotRunning;
          },
          30000);
      const double firstFrame = elapsed.nsecsElapsed() / 1000000.0;
      const bool frame = ready && output.contains("FIRST_FRAME");
      if (!child.waitForFinished(10000)) {
        child.kill();
        child.waitForFinished(2000);
      }
      check(frame && child.exitCode() == 0,
            "fresh process reaches its first real application frame with the cached account");
      std::printf(
          "Fresh process to first app frame, run %d: %.2f ms (%d cached titles, OS cache "
          "retained)\n",
          run + 1, firstFrame, count);
      if (!frame)
        std::fprintf(stderr, "Startup probe: %s %s\n", output.constData(),
                     child.readAllStandardError().constData());
    }
    main->navigateTo(gui::MainWindowPage::List);
    listPage->setViewMode(gui::ListViewMode::Cards);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto* cards = listPage->findChild<QListView*>();
    auto* cardProxy = qobject_cast<gui::AnimeListProxyModel*>(cards->model());
    cardProxy->setFilters({.listStatus = {.status = 0, .anyStatus = true}});
    cardProxy->sort(gui::AnimeListModel::COLUMN_TITLE, Qt::AscendingOrder);
    QCoreApplication::processEvents();
    QPixmapCache::clear();
    clock.restart();
    const bool loaded = waitFor([&] {
      bool ready = true;
      for (const int id : sampleIds) ready &= !gui::imageProvider.loadPoster(id).isNull();
      return ready;
    });
    check(loaded, "cached covers decode asynchronously without a live image service");
    std::printf("24 cached covers to pixmap cache: %.2f ms\n", clock.nsecsElapsed() / 1000000.0);
    clock.restart();
    for (int i = 0; i < 30; ++i) {
      cards->scrollTo(cardProxy->index((i * 5) % 150, 0), QAbstractItemView::PositionAtTop);
      QCoreApplication::processEvents();
      QImage frame(main->size(), QImage::Format_ARGB32_Premultiplied);
      main->QWidget::render(&frame);
    }
    std::printf("Full app card scrolling with cached covers: %.2f ms/frame\n",
                clock.nsecsElapsed() / 30000000.0);
    int updates = 0;
    qint64 longestGap = 0;
    QElapsedTimer gap;
    QTimer heartbeat;
    heartbeat.setInterval(16);
    QObject::connect(&heartbeat, &QTimer::timeout, &app,
                     [&] { longestGap = std::max(longestGap, gap.restart()); });
    gap.start();
    heartbeat.start();
    QTimer pages;
    pages.setInterval(25);
    QObject::connect(&pages, &QTimer::timeout, &app, [&] {
      const int first = (updates * 100) % count;
      auto page = covered.mid(first, std::min(100, count - first));
      anime::db.updateItems(page);
      if (++updates == 10) pages.stop();
    });
    pages.start();
    check(waitFor([&] { return updates == 10 && !pages.isActive(); }),
          "sync-like metadata pages complete while the app event loop runs");
    QCoreApplication::processEvents();
    heartbeat.stop();
    std::printf("UI heartbeat worst gap during 10 metadata pages: %lld ms\n",
                static_cast<long long>(longestGap));
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

  manga::library.flush();
  QThreadPool::globalInstance()->waitForDone();
  delete main;
  QSqlDatabase::database().close();
  std::printf(failures ? "%d integration check(s) failed.\n" : "All integration checks passed.\n",
              failures);
  return failures;
}
