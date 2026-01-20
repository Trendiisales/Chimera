cmake_minimum_required(VERSION 3.16)
project(chimera)

set(CMAKE_CXX_STANDARD 20)

add_executable(chimera
    main.cpp
)

target_include_directories(chimera PRIVATE .)

add_executable(chimera_operator
    gui/operator_server.cpp
)

target_link_libraries(chimera_operator
    pthread
)
