cmake_minimum_required(VERSION 3.20)

project(crypto_engine LANGUAGES CXX)

add_library(crypto_engine STATIC
    src/binance/BinanceDepthFeed.cpp
    src/binance/BinanceRestClient.cpp
    src/binance/BinanceSupervisor.cpp
    src/binance/BinaryLogWriter.cpp
    src/binance/DeltaGate.cpp
    src/binance/OrderBook.cpp
    src/binance/VenueHealth.cpp
)

target_include_directories(crypto_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)

target_link_libraries(crypto_engine
    PRIVATE Threads::Threads
)

set_target_properties(crypto_engine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
