add_executable(chimera_crypto main.cpp)
target_link_libraries(chimera_crypto PRIVATE chimera_core)
set_target_properties(chimera_crypto PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
