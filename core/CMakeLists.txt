add_library(chimera_core STATIC)

target_include_directories(chimera_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

file(GLOB_RECURSE CORE_SOURCES
    src/*.cpp
)

target_sources(chimera_core PRIVATE ${CORE_SOURCES})

target_compile_options(chimera_core PRIVATE -O3 -march=native -pthread)

find_package(Threads REQUIRED)
target_link_libraries(chimera_core PUBLIC Threads::Threads)
