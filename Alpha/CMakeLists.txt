cmake_minimum_required(VERSION 3.16)
project(Alpha VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Find OpenSSL (required for FIX SSL)
find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

# Find threads
find_package(Threads REQUIRED)

# Main executable
add_executable(alpha
    src/main.cpp
)

# Link libraries
target_link_libraries(alpha PRIVATE
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Platform-specific
if(WIN32)
    target_link_libraries(alpha PRIVATE ws2_32 crypt32)
endif()

# Installation
install(TARGETS alpha DESTINATION bin)
install(FILES config/config.ini.template DESTINATION etc/alpha)

# Print configuration
message(STATUS "")
message(STATUS "╔════════════════════════════════════════════════════════════════╗")
message(STATUS "║  Alpha Trading System v${PROJECT_VERSION}                                 ║")
message(STATUS "╠════════════════════════════════════════════════════════════════╣")
message(STATUS "║  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "║  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "║  OpenSSL: ${OPENSSL_VERSION}")
message(STATUS "╚════════════════════════════════════════════════════════════════╝")
message(STATUS "")
