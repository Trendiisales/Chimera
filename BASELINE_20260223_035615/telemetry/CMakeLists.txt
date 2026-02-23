cmake_minimum_required(VERSION 3.16)
project(chimera_telemetry)

add_library(chimera_telemetry
    TelemetryBus.cpp
    TelemetryJson.cpp
    TelemetrySinkStdout.cpp
    TelemetryWsServer.cpp
)

target_include_directories(chimera_telemetry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(chimera_telemetry PROPERTIES CXX_STANDARD 17)
