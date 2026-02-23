cmake_minimum_required(VERSION 3.16)
project(chimera_latency)

add_library(chimera_latency
    LatencyAttributionEngine.cpp
    TelemetrySinkStdout.cpp
)

target_include_directories(chimera_latency PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(chimera_latency PROPERTIES CXX_STANDARD 17)
