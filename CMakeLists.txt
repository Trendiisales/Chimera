cmake_minimum_required(VERSION 3.16)
project(chimera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include_directories(
    ${CMAKE_SOURCE_DIR}
    core/include
)

# Find Boost
find_package(Boost REQUIRED COMPONENTS system json)
include_directories(${Boost_INCLUDE_DIRS})

# Gather all source files
file(GLOB_RECURSE CORE_SOURCES core/src/*.cpp)
file(GLOB_RECURSE TELEMETRY_SOURCES telemetry/*.cpp)
file(GLOB_RECURSE EXECUTION_SOURCES execution/*.cpp)
file(GLOB_RECURSE ROUTER_SOURCES router/*.cpp)
file(GLOB CORE_ROOT_SOURCES core/*.cpp)

# Combine all sources
set(CHIMERA_SOURCES
    ${CORE_SOURCES}
    ${TELEMETRY_SOURCES}
    ${EXECUTION_SOURCES}
    ${ROUTER_SOURCES}
    ${CORE_ROOT_SOURCES}
    main.cpp
)

# Filter out any .bak files
list(FILTER CHIMERA_SOURCES EXCLUDE REGEX "\\.bak")

add_executable(chimera ${CHIMERA_SOURCES})

# Link libraries
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Add GUI subdirectory
add_subdirectory(gui)

target_link_libraries(chimera PRIVATE 
    chimera_gui
    Threads::Threads
    ${Boost_LIBRARIES}
    curl
    OpenSSL::SSL
    OpenSSL::Crypto
)
