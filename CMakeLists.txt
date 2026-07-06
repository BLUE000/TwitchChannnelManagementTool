cmake_minimum_required(VERSION 3.20)
project(TwitchChannelManagementTool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Qt6 パッケージの検索
find_package(Qt6 COMPONENTS Core Widgets Network WebSockets REQUIRED)

# オプション定義
option(ENABLE_TEST_MODE "Build with stubs instead of real network services" OFF)
option(BUILD_TESTING "Build the test suite" OFF)

# インクルード・ライブラリ設定
set(TRANSCIPHER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/TransCipher-Dist/include")
set(TRANSCIPHER_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/lib/TransCipher-Dist/lib/libTransCipher.dll.a")

# サブモジュールの追加
add_subdirectory(lib/TrustChain)

# ソースコード定義
set(CORE_SOURCES
    src/main.cpp
    src/core/AppController.cpp
    src/core/PluginLoader.cpp
    src/core/SignalDispatcher.cpp
    src/modules/logger/Logger.cpp
    src/modules/twitch/TwitchEventCollector.cpp
    src/modules/obs/ObsHttpWebSocketServer.cpp
    src/modules/tts/BouyomiClient.cpp
    src/modules/tts/VoiceVoxClient.cpp
    src/modules/tts/TtsManager.cpp
    src/ui/MainWindow.cpp
    src/ui/SettingsTab.cpp
)

# ヘッダー・リソース
set(CORE_HEADERS
    src/shared/plugin_interface.h
    src/core/AppController.h
    src/core/PluginLoader.h
    src/core/SignalDispatcher.h
    src/core/twitch_credentials.h
    src/modules/logger/Logger.h
    src/modules/twitch/TwitchEventCollector.h
    src/modules/obs/ObsHttpWebSocketServer.h
    src/modules/tts/ITtsClient.h
    src/modules/tts/BouyomiClient.h
    src/modules/tts/VoiceVoxClient.h
    src/modules/tts/TtsManager.h
    src/ui/MainWindow.h
    src/ui/SettingsTab.h
)

set(CORE_UIS
    src/ui/MainWindow.ui
    src/ui/SettingsTab.ui
)

# EXE または テスト用EXE の定義
if (ENABLE_TEST_MODE)
    add_executable(TwitchChannelManagementToolTestMode WIN32
        ${CORE_SOURCES}
        ${CORE_HEADERS}
        ${CORE_UIS}
    )
    set(MAIN_TARGET TwitchChannelManagementToolTestMode)
    target_compile_definitions(${MAIN_TARGET} PRIVATE ENABLE_TEST_MODE)
else()
    add_executable(TwitchChannelManagementTool WIN32
        ${CORE_SOURCES}
        ${CORE_HEADERS}
        ${CORE_UIS}
    )
    set(MAIN_TARGET TwitchChannelManagementTool)
endif()

# インクルードディレクトリの設定
target_include_directories(${MAIN_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/modules"
    "${TRANSCIPHER_INCLUDE_DIR}"
)

# ライブラリのリンク
target_link_libraries(${MAIN_TARGET} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    trustchain
    "${TRANSCIPHER_LIBRARY}"
)

# Windows環境用の追加のライブラリ（Crypt32.lib / dpapi 用）
if (WIN32)
    target_link_libraries(${MAIN_TARGET} PRIVATE Crypt32 winmm)
endif()

# TrustChain 出自証明マクロセットアップ
include(lib/TrustChain/cmake/trustchain.cmake)
trustchain_setup_target(${MAIN_TARGET})

# テストビルド
if (BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()
