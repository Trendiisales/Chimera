add_executable(gui_hub main.cpp)
target_link_libraries(gui_hub PRIVATE chimera_core)
set_target_properties(gui_hub PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
