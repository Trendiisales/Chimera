cmake_minimum_required(VERSION 3.16)
project(chimera_replay)

add_library(chimera_replay
    ReplayLog.cpp
    ReplayRecorder.cpp
    ReplayEngine.cpp
    PostTradeAnalyzer.cpp
)

target_include_directories(chimera_replay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(chimera_replay PROPERTIES CXX_STANDARD 17)
