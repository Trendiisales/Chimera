cmake_minimum_required(VERSION 3.16)
project(chimera)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_custom_target(guard
    COMMAND ${CMAKE_SOURCE_DIR}/build_guard.sh
)

add_executable(chimera
    main.cpp
    core/spine.cpp
    engines/BTCascade.cpp
    engines/ETHSniper.cpp
    engines/MeanReversion.cpp
)

add_dependencies(chimera guard)
