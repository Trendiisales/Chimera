cmake_minimum_required(VERSION 3.16)
project(chimera CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)
pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json)

add_executable(chimera
    main.cpp
    market/binance_adapter.cpp
    exec/binance_executor.cpp
)

target_include_directories(chimera PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${LIBWEBSOCKETS_INCLUDE_DIRS}
    ${NLOHMANN_JSON_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(chimera PRIVATE
    Threads::Threads
    ${LIBWEBSOCKETS_LIBRARIES}
    ${CURL_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_options(chimera PRIVATE
    ${LIBWEBSOCKETS_CFLAGS_OTHER}
    -Wall
    -Wextra
    -Wpedantic
)
