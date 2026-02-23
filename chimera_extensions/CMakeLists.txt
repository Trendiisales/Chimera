cmake_minimum_required(VERSION 3.15)

project(ChimeraExtensions VERSION 1.0.0 LANGUAGES CXX)

# ==================== COMPILER SETTINGS ====================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        add_compile_options(/O2 /GL /arch:AVX2)
    else()
        add_compile_options(-O3 -march=native -mtune=native)
    endif()
endif()

# Warning flags
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ==================== INCLUDE DIRECTORIES ====================

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/engines
    ${CMAKE_CURRENT_SOURCE_DIR}/allocation
    ${CMAKE_CURRENT_SOURCE_DIR}/risk
    ${CMAKE_CURRENT_SOURCE_DIR}/telemetry
    ${CMAKE_CURRENT_SOURCE_DIR}/spine
    ${CMAKE_CURRENT_SOURCE_DIR}/infra
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/integration
)

# ==================== HEADER-ONLY LIBRARIES ====================

add_library(chimera_engines INTERFACE)
target_include_directories(chimera_engines INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/engines
)

add_library(chimera_allocation INTERFACE)
target_include_directories(chimera_allocation INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/allocation
)

add_library(chimera_risk INTERFACE)
target_include_directories(chimera_risk INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/risk
)

add_library(chimera_telemetry INTERFACE)
target_include_directories(chimera_telemetry INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/telemetry
)

add_library(chimera_spine INTERFACE)
target_include_directories(chimera_spine INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/spine
)

add_library(chimera_infra INTERFACE)
target_include_directories(chimera_infra INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/infra
)

add_library(chimera_core INTERFACE)
target_include_directories(chimera_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
)
target_link_libraries(chimera_core INTERFACE
    chimera_engines
    chimera_allocation
    chimera_risk
    chimera_telemetry
)

add_library(chimera_integration INTERFACE)
target_include_directories(chimera_integration INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/integration
)
target_link_libraries(chimera_integration INTERFACE
    chimera_core
    chimera_spine
    chimera_infra
)

# ==================== EXAMPLE EXECUTABLE ====================

add_executable(chimera_enhanced_example
    examples/integrated_main.cpp
)

target_link_libraries(chimera_enhanced_example
    chimera_integration
)

# Platform-specific threading library
find_package(Threads REQUIRED)
target_link_libraries(chimera_enhanced_example Threads::Threads)

# ==================== INSTALLATION ====================

install(TARGETS chimera_enhanced_example
    RUNTIME DESTINATION bin
)

install(DIRECTORY
    engines
    allocation
    risk
    telemetry
    spine
    infra
    core
    integration
    DESTINATION include/chimera_extensions
    FILES_MATCHING PATTERN "*.hpp"
)

# ==================== TESTING (Optional) ====================

option(BUILD_TESTS "Build test suite" OFF)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ==================== DOCUMENTATION ====================

message(STATUS "")
message(STATUS "Chimera Extensions Configuration:")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "Components:")
message(STATUS "  [✓] Metal Structure Engine")
message(STATUS "  [✓] Enhanced Capital Allocator")
message(STATUS "  [✓] Risk Governor")
message(STATUS "  [✓] Telemetry Collector")
message(STATUS "  [✓] Execution Spine")
message(STATUS "  [✓] SPSC Ring Buffer")
message(STATUS "  [✓] Unified Engine Coordinator")
message(STATUS "  [✓] Integration Layer")
message(STATUS "")
