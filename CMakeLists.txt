cmake_minimum_required(VERSION 3.20)
project(chimera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(crypto_engine)

add_executable(chimera
    main.cpp
)

target_link_libraries(chimera
    PRIVATE crypto_engine
)
