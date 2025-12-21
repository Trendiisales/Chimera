cmake_minimum_required(VERSION 3.20)
project(Chimera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Sub-engines ----
add_subdirectory(crypto_engine)
add_subdirectory(cfd_engine)

# ---- Main executable ----
add_executable(chimera
    main.cpp

    # accounting
    src/accounting/PnlLedger.cpp
    src/accounting/DailyPnlStore.cpp

    # execution
    src/execution/ExecutionEngine.cpp

    # microstructure
    src/micro/MicrostructureEngine.cpp

    # strategy
    src/strategy/StrategyEngine.cpp

    # latency
    src/latency/LatencyRegistry.cpp

    # gui
    src/gui/MetricsHttpServer.cpp
)

target_link_libraries(chimera
    crypto_engine
    cfd_engine
)

target_include_directories(chimera PUBLIC
    include
    src
    crypto_engine/include
)
