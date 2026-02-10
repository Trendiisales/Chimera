cmake_minimum_required(VERSION 3.10)
project(chimera)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/cfd_engine/include
)

file(GLOB_RECURSE CFD_SOURCES "cfd_engine/src/*.cpp")

add_executable(chimera
    src/main.cpp
    src/shadow/SymbolExecutor.cpp
    src/shadow/MultiSymbolExecutor.cpp
    src/shadow/CrashHandler.cpp
    src/shadow/WatchdogThread.cpp
    src/shadow/JournalWriter.cpp
    src/shadow/EquityCurve.cpp
    src/core/TradeLedger.cpp
    src/execution/ExecutionGovernor.cpp
    ${CFD_SOURCES}
)

target_link_libraries(chimera
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    dl
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -O3")

add_custom_command(TARGET chimera POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config.ini
        ${CMAKE_CURRENT_BINARY_DIR}/config.ini
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/chimera_dashboard.html
        ${CMAKE_CURRENT_BINARY_DIR}/chimera_dashboard.html
    COMMENT "Copying config files"
)

# GUI sources
add_library(gui
    src/gui/GuiState.cpp
    src/gui/TradeBlotter.cpp
    src/gui/LatencyStats.cpp
    src/gui/PnLModel.cpp
    src/gui/WsServer.cpp
    src/gui/GUIBroadcaster.cpp
    src/gui/ExecutionSnapshotJSON.cpp
)

target_include_directories(gui PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(chimera gui)
