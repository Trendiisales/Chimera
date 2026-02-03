cmake_minimum_required(VERSION 3.16)
project(chimera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/tier1
    ${CMAKE_SOURCE_DIR}/src/tier2
)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL    REQUIRED)
find_package(Boost   REQUIRED COMPONENTS system)

add_executable(chimera
    src/main_binance_shadow.cpp
    src/runtime/CpuPinning.cpp
    src/runtime/ThreadModel.cpp
    src/runtime/LiveArmSystem.cpp
    src/runtime/ContextSnapshotter.cpp
    src/runtime/ColdStartReconciler.cpp
    src/runtime/ExchangeTruthLoop.cpp
    src/exchange/binance/BinanceRestClient.cpp
    src/exchange/binance/BinanceWSMarket.cpp
    src/exchange/binance/BinanceWSUser.cpp
    src/exchange/binance/BinanceWSExecution.cpp
    src/exchange/binance/BinanceReconciler.cpp
    src/execution/OrderStateMachine.cpp
    src/execution/ExecutionThrottle.cpp
    src/execution/CancelReplaceCoalescer.cpp
    src/execution/QueuePositionModel.cpp
    src/execution/ExecutionRouter.cpp
    src/execution/LatencyGovernor.cpp
    src/execution/QueueDecayGovernor.cpp
    src/control/PnLGovernor.cpp
    src/control/DeskArbiter.cpp
    src/control/UnwindCoordinator.cpp
    src/control/PositionGate.cpp
    src/control/DriftFilter.cpp
    src/control/FundingEngine.cpp
    src/control/VolatilityRegime.cpp
    src/control/SessionEngine.cpp
    src/control/ExecutionAlpha.cpp
    src/control/Microprice.cpp
    src/control/QueueModel.cpp
    src/risk/ExchangeTruthReconciler.cpp
    src/risk/DriftDetector.cpp
    src/risk/GlobalRiskGovernor.cpp
    src/strategy/StrategyRunner.cpp
    src/strategy/QueueMarketMaker.cpp
    src/strategy/ETHFade.cpp
    src/strategy/SOLFade.cpp
    src/strategy/ImpulseReversion.cpp
    src/strategy/PortfolioSkewTrader.cpp
    src/strategy/BTCascade.cpp
    src/strategy/ETHSniper.cpp
    src/strategy/MeanReversion.cpp
    src/forensics/BinaryRecorder.cpp
    src/forensics/EdgeAttribution.cpp
    src/telemetry/TelemetryState.cpp
    src/telemetry/HttpServer.cpp
)

target_include_directories(chimera PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/tier1
    ${CMAKE_SOURCE_DIR}/src/tier2
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(chimera
    Threads::Threads
    ${OPENSSL_LIBRARIES}
    ${CURL_LIBRARIES}
    ${Boost_LIBRARIES}
)

target_compile_options(chimera PRIVATE -O3 -march=native -pthread)
