cmake_minimum_required(VERSION 3.16)
project(chimera LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
add_subdirectory(crypto_engine)

add_executable(chimera
    src/main.cpp
    src/execution/ExecutionEngine.cpp
    src/execution/PositionTracker.cpp
    src/risk/RiskManager.cpp
    src/accounting/DailyPnlStore.cpp
    src/micro/MicrostructureEngine.cpp
    src/strategy/LatencyGlobals.cpp
    src/engine/IntentQueue.cpp
    src/strategy/StrategyRegistry.cpp
    src/strategy/DummyStrategy.cpp
    src/metrics/HttpMetricsServer.cpp
)

add_executable(chimera_full
    main_full.cpp
    src/execution/ExecutionEngine.cpp
    src/execution/PositionTracker.cpp
    src/risk/RiskManager.cpp
    src/accounting/DailyPnlStore.cpp
    src/micro/MicrostructureEngine.cpp
    src/strategy/LatencyGlobals.cpp
    src/engine/IntentQueue.cpp
    src/strategy/StrategyRegistry.cpp
    src/strategy/DummyStrategy.cpp
    src/metrics/HttpMetricsServer.cpp
)

target_include_directories(chimera PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/crypto_engine/include)
target_include_directories(chimera_full PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/crypto_engine/include)
target_link_libraries(chimera crypto_engine Threads::Threads)
target_link_libraries(chimera_full crypto_engine Threads::Threads)
