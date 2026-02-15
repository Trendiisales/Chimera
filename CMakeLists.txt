cmake_minimum_required(VERSION 3.16)
project(chimera_production)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -pthread")

find_package(OpenSSL REQUIRED)

add_executable(chimera
    main.cpp
)

target_include_directories(chimera PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(chimera
    pthread
    OpenSSL::SSL
    OpenSSL::Crypto
)
