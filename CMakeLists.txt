cmake_minimum_required(VERSION 3.16)
project(chimera)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)

include_directories(
    include
    src
)

file(GLOB_RECURSE SRC_FILES
    src/*.cpp
)

# Exclude legacy router
list(FILTER SRC_FILES EXCLUDE REGEX "ExecutionRouter.cpp$")

add_executable(chimera ${SRC_FILES})

target_link_libraries(chimera
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
)
