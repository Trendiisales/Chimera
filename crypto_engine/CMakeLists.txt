cmake_minimum_required(VERSION 3.20)

add_library(crypto_engine
    src/binance/BinanceRestClient.cpp
    src/binance/BinanceDepthParser.cpp
    src/binance/TlsWebSocket.cpp
    src/binance/BinanceDepthAdapterWS.cpp
    src/binance/BinanceHFTFeed.cpp
    src/binance/BinanceReplay.cpp
    src/binance/BinanceSupervisor.cpp
    src/binance/BinaryLogWriter.cpp
    src/binance/PrometheusMetrics.cpp
    src/binance/PrometheusServer.cpp
    src/binance/ExecutionLogger.cpp
    src/binance/MidPriceStrategy.cpp
    src/binance/StrategyBridge.cpp
)

target_include_directories(crypto_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(OpenSSL REQUIRED)

target_link_libraries(crypto_engine
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
)
