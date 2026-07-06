# Google Test の取得 (FetchContent を使用して自動ダウンロード)
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# ランタイムライブラリ競合の防止設定
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# テストに必要なQtモジュール
find_package(Qt6 COMPONENTS Core Test Network WebSockets REQUIRED)

# インクルードディレクトリ
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/modules
    ${CMAKE_SOURCE_DIR}/lib/TransCipher-Dist/include
    ${CMAKE_SOURCE_DIR}/lib/TrustChain/include
)

# モックおよびテスト用共通ソース
set(TEST_COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/core/AppController.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PluginLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/SignalDispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/logger/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/twitch/TwitchEventCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/obs/ObsHttpWebSocketServer.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/tts/BouyomiClient.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/tts/VoiceVoxClient.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/tts/TtsManager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/SettingsTab.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/SettingsTab.ui
    ${CMAKE_SOURCE_DIR}/src/ui/MainWindow.ui
)

# 単体テストターゲット
add_executable(run_unit_tests
    test_main.cpp
    unit/test_logger.cpp
    unit/test_plugin_loader.cpp
    unit/test_tts_clients.cpp
    unit/test_obs_server.cpp
    ${TEST_COMMON_SOURCES}
)
target_compile_definitions(run_unit_tests PRIVATE ENABLE_TEST_MODE)

# 結合テストターゲット
add_executable(run_integration_tests
    test_main.cpp
    integration/test_integration.cpp
    ${TEST_COMMON_SOURCES}
)
target_compile_definitions(run_integration_tests PRIVATE ENABLE_TEST_MODE)

# リンク設定
foreach(TEST_TARGET run_unit_tests run_integration_tests)
    target_link_libraries(${TEST_TARGET} PRIVATE
        Qt6::Core
        Qt6::Test
        Qt6::Network
        Qt6::WebSockets
        trustchain
        gtest
        "${CMAKE_SOURCE_DIR}/lib/TransCipher-Dist/lib/libTransCipher.dll.a"
    )
    if (WIN32)
        target_link_libraries(${TEST_TARGET} PRIVATE Crypt32 winmm)
    endif()
    add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
endforeach()
