add_library(chimera_gui
    src/gui_snapshot_bus.cpp
    src/live_operator_server.cpp
)

target_include_directories(chimera_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
