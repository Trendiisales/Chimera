cmake_minimum_required(VERSION 3.10)
project(ChimeraReal)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIR}
    ${LIBWEBSOCKETS_INCLUDE_DIRS}
)

set(SOURCES
    src/main.cpp
    src/BinanceRestClient.cpp
    src/BinanceDepthWS.cpp
    src/BinanceUserDataWS.cpp
    src/BinanceAggTradeWS.cpp
    src/FadeETH.cpp
)

add_executable(chimera_real ${SOURCES})

target_link_libraries(chimera_real
    ${OPENSSL_LIBRARIES}
    ${CURL_LIBRARIES}
    ${LIBWEBSOCKETS_LIBRARIES}
    pthread
)
