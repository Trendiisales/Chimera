cmake_minimum_required(VERSION 3.16)
project(chimera_sizing)

add_library(chimera_sizing
    ConfidenceWeightedSizer.cpp
    SizingSinkStdout.cpp
)

target_include_directories(chimera_sizing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(chimera_sizing PROPERTIES CXX_STANDARD 17)
