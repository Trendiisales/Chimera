cmake_minimum_required(VERSION 3.16)
project(Chimera VERSION 4.14.11 LANGUAGES CXX)

# =============================================================================
# CHIMERA v4.14.11 - UNIFIED ENGINE (OpenAPI)
# =============================================================================
# SYMBOLS: XAUUSD, NAS100, US30 ONLY
# NO CRYPTO - NO FOREX - CLEAN ARCHITECTURE
#
# Build: mkdir build && cd build && cmake .. && make
# =============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Release by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -O2)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# =============================================================================
# OPTIONS
# =============================================================================
option(BUILD_TESTS "Build test utilities" OFF)

# =============================================================================
# CHIMERA - PRIMARY TARGET
# =============================================================================
# Single unified binary for XAUUSD + NAS100 + US30
# =============================================================================

add_executable(chimera src/main.cpp)

target_include_directories(chimera PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/engines
    ${CMAKE_SOURCE_DIR}/include/shared
    ${CMAKE_SOURCE_DIR}/cfd_engine/include
    ${CMAKE_SOURCE_DIR}/cfd_engine/include/fix
    ${CMAKE_SOURCE_DIR}/cfd_engine/include/openapi
)

target_link_libraries(chimera PRIVATE 
    OpenSSL::SSL 
    OpenSSL::Crypto 
    Threads::Threads
)

# Copy config.ini to build directory after build
add_custom_command(TARGET chimera POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config.ini
        $<TARGET_FILE_DIR:chimera>/config.ini
    COMMENT "Copying config.ini to build directory"
)

# Also copy dashboard
add_custom_command(TARGET chimera POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/chimera_dashboard.html
        $<TARGET_FILE_DIR:chimera>/chimera_dashboard.html
    COMMENT "Copying dashboard to build directory"
)

# =============================================================================
# WINDOWS-SPECIFIC
# =============================================================================
if(WIN32)
    target_link_libraries(chimera PRIVATE ws2_32)
endif()

# =============================================================================
# TEST UTILITIES (optional)
# =============================================================================
if(BUILD_TESTS)
    add_executable(fix_diag src/fix_diag.cpp)
    target_include_directories(fix_diag PRIVATE 
        ${CMAKE_SOURCE_DIR}/cfd_engine/include
        ${CMAKE_SOURCE_DIR}/include/shared
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(fix_diag PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    
    if(WIN32)
        target_link_libraries(fix_diag PRIVATE ws2_32)
    endif()
endif()

# =============================================================================
# BUILD SUMMARY
# =============================================================================
message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════════════")
message(STATUS "  CHIMERA v4.14.2 - UNIFIED ENGINE")
message(STATUS "═══════════════════════════════════════════════════════════════")
message(STATUS "  TARGET:   chimera")
message(STATUS "  SYMBOLS:  XAUUSD, NAS100, US30")
message(STATUS "  PROTOCOL: cTrader OpenAPI (Protobuf/SSL)")
message(STATUS "")
message(STATUS "  NAS100 BACKTEST-LOCKED SETTINGS:")
message(STATUS "    OR Window:      15 min")
message(STATUS "    OR Range:       25-120 pts")
message(STATUS "    Sweep Dist:     35 pts")
message(STATUS "    Revert Trigger: 18 pts")
message(STATUS "    Size Mult:      2.0x")
message(STATUS "═══════════════════════════════════════════════════════════════")
message(STATUS "")
