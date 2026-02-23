add_library(chimera_gui
    TelemetryServer.cpp
)

target_include_directories(chimera_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chimera_gui pthread)
