cmake_minimum_required(VERSION 3.20)

add_library(crypto_engine STATIC
    src/binance/BinanceDepthFeed.cpp
    src/binance/BinanceRestClient.cpp
    src/binance/BinanceSupervisor.cpp
    src/binance/BinaryLogWriter.cpp
    src/binance/DeltaGate.cpp
    src/binance/OrderBook.cpp
    src/binance/PrometheusServer.cpp
    src/binance/VenueHealth.cpp
)

target_include_directories(crypto_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(crypto_engine PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

find_package(Threads REQUIRED)
target_link_libraries(crypto_engine PRIVATE Threads::Threads)
