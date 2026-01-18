add_executable(chimera_supervisor main.cpp)
target_link_libraries(chimera_supervisor PRIVATE chimera_core)
set_target_properties(chimera_supervisor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
