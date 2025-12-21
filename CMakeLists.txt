cmake_minimum_required(VERSION 3.16)
project(chimera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------
# crypto_engine (static lib)
# -------------------------
add_library(crypto_engine STATIC
    crypto_engine/src/binance/BinanceRestClient.cpp
    crypto_engine/src/binance/BinanceSupervisor.cpp
    crypto_engine/src/binance/BinaryLogWriter.cpp
    crypto_engine/src/binance/DeltaGate.cpp
    crypto_engine/src/binance/OrderBook.cpp
    crypto_engine/src/binance/VenueHealth.cpp
)

target_include_directories(crypto_engine PUBLIC
    crypto_engine/include
)

# -------------------------
# chimera (main executable)
# -------------------------
add_executable(chimera
    main.cpp

    # microstructure
    src/micro/MicrostructureEngine.cpp

    # strategy
    src/strategy/StrategyEngine.cpp

    # execution / fills
    src/execution/ExecutionEngine.cpp
    src/execution/PositionTracker.cpp

    # accounting / risk
    src/accounting/DailyPnlStore.cpp
    src/risk/RiskManager.cpp
)

target_include_directories(chimera PUBLIC
    include
    crypto_engine/include
)

target_link_libraries(chimera
    crypto_engine
)

# FIX 3: required link units
set_property(TARGET chimera APPEND PROPERTY SOURCES
    src/strategy/LatencyGlobals.cpp
    crypto_engine/src/binance/BinanceDepthFeed.cpp
)
