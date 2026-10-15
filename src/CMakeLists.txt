include(QtSetup)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(taiga)

target_sources(taiga PRIVATE
	base/atf.cpp
	base/atf.hpp
	base/chrono.cpp
	base/chrono.hpp
	base/file.cpp
	base/file.hpp
	base/log.cpp
	base/log.hpp
	base/preprocessor.h
	base/rss.hpp
	base/settings.cpp
	base/settings.hpp
	base/string.cpp
	base/string.hpp
	base/xml.cpp
	base/xml.hpp

	compat/anime.cpp
	compat/anime.hpp
	compat/common.cpp
	compat/common.hpp
	compat/history.cpp
	compat/history.hpp
	compat/list.cpp
	compat/list.hpp
	compat/settings.cpp
	compat/settings.hpp

	media/anime_db.cpp
	media/anime_db.hpp
	media/anime_history.cpp
	media/anime_history.hpp
	media/anime_list_export.cpp
	media/anime_list_export.hpp
	media/anime_list_utils.cpp
	media/anime_list_utils.hpp
	media/anime_list.hpp
	media/anime_season.cpp
	media/anime_season.hpp
	media/anime_query.cpp
	media/anime_query.hpp
	media/account_stats.cpp
	media/account_stats.hpp
	media/account_activity.cpp
	media/account_activity.hpp
	media/manga_io.cpp
	media/manga_library.cpp
	media/manga_library.hpp
	media/anime_stats.cpp
	media/anime_stats.hpp
	media/anime_settings.hpp
	media/anime_utils.cpp
	media/anime_utils.hpp
	media/anime.hpp
	media/manga.hpp

	sync/anilist/anilist.cpp
	sync/anilist/anilist.hpp
	sync/anilist/anilist_auth.cpp
	sync/anilist/anilist_error.cpp
	sync/anilist/anilist_error.hpp
	sync/anilist/anilist_parsers.cpp
	sync/anilist/anilist_parsers.hpp
	sync/anilist/anilist_ratings.cpp
	sync/anilist/anilist_ratings.hpp
	sync/anilist/anilist_utils.cpp
	sync/anilist/anilist_utils.hpp
	sync/kitsu/kitsu.cpp
	sync/kitsu/kitsu.hpp
	sync/kitsu/kitsu_auth.cpp
	sync/kitsu/kitsu_error.cpp
	sync/kitsu/kitsu_error.hpp
	sync/kitsu/kitsu_parsers.cpp
	sync/kitsu/kitsu_parsers.hpp
	sync/kitsu/kitsu_ratings.cpp
	sync/kitsu/kitsu_ratings.hpp
	sync/kitsu/kitsu_utils.cpp
	sync/kitsu/kitsu_utils.hpp
	sync/myanimelist/myanimelist.cpp
	sync/myanimelist/myanimelist.hpp
	sync/myanimelist/myanimelist_auth.cpp
	sync/myanimelist/myanimelist_manga.cpp
	sync/myanimelist/myanimelist_error.cpp
	sync/myanimelist/myanimelist_error.hpp
	sync/myanimelist/myanimelist_parsers.cpp
	sync/myanimelist/myanimelist_parsers.hpp
	sync/myanimelist/myanimelist_ratings.cpp
	sync/myanimelist/myanimelist_ratings.hpp
	sync/myanimelist/myanimelist_utils.cpp
	sync/myanimelist/myanimelist_utils.hpp
	sync/queue.cpp
	sync/queue.hpp
	sync/service.cpp
	sync/service.hpp

	taiga/accounts.cpp
	taiga/accounts.hpp
	taiga/application.cpp
	taiga/application.hpp
	taiga/config.h
	taiga/network.cpp
	taiga/network.hpp
	taiga/orange.cpp
	taiga/orange.hpp
	taiga/path.cpp
	taiga/path.hpp
	taiga/session.cpp
	taiga/session.hpp
	taiga/settings.cpp
	taiga/sharing.cpp
	taiga/sharing.hpp
	taiga/settings.hpp
	taiga/version.cpp
	taiga/version.hpp

	track/episode.cpp
	track/episode.hpp
	track/media.cpp
	track/media.hpp
	track/media_player.cpp
	track/media_player.hpp
	track/play.cpp
	track/play.hpp
	track/recognition.cpp
	track/recognition.hpp
	track/recognition_cache.cpp
	track/recognition_cache.hpp
	track/recognition_normalize.cpp
	track/recognition_normalize.hpp
	track/recognition_path.cpp
	track/recognition_path.hpp
	track/recognition_relations.cpp
	track/recognition_relations.hpp
	track/recognition_validate.cpp
	track/recognition_validate.hpp
	track/feed.cpp
	track/feed.hpp
	track/feed_filter.cpp
	track/feed_filter.hpp
	track/scanner.cpp
	track/scanner.hpp
	track/update_decision.cpp
	track/update_decision.hpp
	track/update_session.cpp
	track/update_session.hpp
	track/update_state.hpp
	track/update_trigger.hpp

	main.cpp
)

target_link_libraries(taiga PRIVATE
	Qt6::Core
	Qt6::Gui
	Qt6::Network
	Qt6::Sql
	Qt6::Widgets
	taiga-config
	taiga-deps
	taiga-gui
	taiga-resources
)

if (TAIGA_PORTABLE)
	target_compile_definitions(taiga PRIVATE TAIGA_PORTABLE)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	set_target_properties(taiga PROPERTIES OUTPUT_NAME Taiga)
	set_target_properties(taiga PROPERTIES WIN32_EXECUTABLE ON)
endif()

# Self-checks for pure logic; run `taiga-checks` after building.
add_executable(taiga-checks
	checks.cpp
	base/atf.cpp
	track/feed_filter.cpp
	media/account_stats.cpp
	media/manga_io.cpp
	media/anime_query.cpp
	base/chrono.cpp
)
target_link_libraries(taiga-checks PRIVATE Qt6::Core)
if(BUILD_TESTING)
  add_test(NAME taiga-logic COMMAND taiga-checks)
  set_tests_properties(taiga-logic PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

add_subdirectory(gui)
add_subdirectory(resources)

qt_add_translations(taiga
	SOURCE_TARGETS taiga-gui
	TS_FILE_BASE taiga
	TS_FILE_DIR resources/translations
	LUPDATE_OPTIONS -no-obsolete -locations none
)

# Integration checks use the real app code with a temporary data directory.
if(BUILD_TESTING)
  get_target_property(taiga_check_sources taiga SOURCES)
  list(FILTER taiga_check_sources EXCLUDE REGEX "(^|/)main\\.cpp$")
  add_executable(taiga-integration-checks ui_checks.cpp ${taiga_check_sources})
  target_compile_definitions(taiga-integration-checks PRIVATE TAIGA_TESTING TAIGA_PORTABLE)
  target_link_libraries(taiga-integration-checks PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql
    Qt6::Widgets taiga-config taiga-deps taiga-gui taiga-resources)
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(taiga-integration-checks PRIVATE Psapi)
    set(taiga_test_platform windows)
  else()
    set(taiga_test_platform offscreen)
  endif()
  add_test(NAME taiga-ui COMMAND taiga-integration-checks)
  set_tests_properties(taiga-ui PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=${taiga_test_platform};QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
