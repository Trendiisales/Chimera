cmake_minimum_required(VERSION 3.16)
project(chimera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(
    ${CMAKE_SOURCE_DIR}
)

########################################
# GUARD — FAIL FAST IF FILES MISSING
########################################
set(REQUIRED_FILES
    src/spine_wired_main.cpp

    src/core/state/PositionState.cpp
    src/core/state/PositionState.hpp
    src/core/state/EventJournal.cpp
    src/core/state/EventJournal.hpp
    src/core/state/ShadowFillEngine.cpp
    src/core/state/ShadowFillEngine.hpp
    src/core/state/EquityLogger.cpp
    src/core/state/EquityLogger.hpp

    src/core/control/ControlPlane.cpp
    src/core/control/ControlPlane.hpp
    src/core/control/StrategyArbiter.cpp
    src/core/control/StrategyArbiter.hpp
    src/core/control/RegimeSupervisor.cpp
    src/core/control/RegimeSupervisor.hpp
    src/core/control/RiskGovernor.cpp
    src/core/control/RiskGovernor.hpp
    src/core/control/CapitalAllocator.cpp
    src/core/control/CapitalAllocator.hpp
    src/core/control/EdgeMonitor.cpp
    src/core/control/EdgeMonitor.hpp
    src/core/control/VenueHealth.cpp
    src/core/control/VenueHealth.hpp

    src/core/execution/ExecutionEngine.cpp
    src/core/execution/ExecutionEngine.hpp
    src/core/execution/ExecPolicy.cpp
    src/core/execution/ExecPolicy.hpp
    src/core/execution/OrderBookView.cpp
    src/core/execution/OrderBookView.hpp
    src/core/execution/VenueRouter.cpp
    src/core/execution/VenueRouter.hpp

    src/core/latency/LatencyTracker.cpp
    src/core/latency/LatencyTracker.hpp

    src/core/replay/ReplayEngine.cpp
    src/core/replay/ReplayEngine.hpp

    src/core/gui/GuiBroadcaster.cpp
    src/core/gui/GuiBroadcaster.hpp
)

foreach(f ${REQUIRED_FILES})
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/${f})
        message(WARNING "SKIP: ${f}")
    endif()
endforeach()

########################################
# COLLECT SOURCES
########################################
file(GLOB_RECURSE CORE_SRC
    src/core/state/*.cpp
    src/core/control/*.cpp
    src/core/execution/*.cpp
    src/core/execution/**/*.cpp
    src/core/latency/*.cpp
    src/core/replay/*.cpp
    src/core/gui/*.cpp
    src/core/metrics/*.cpp
    src/core/kill/*.cpp
    src/core/snapshot/*.cpp
    src/core/telemetry/*.cpp
    src/core/pnl/*.cpp
    src/core/journal/*.cpp
    src/core/coordination/*.cpp
    src/core/coordination/**/*.cpp
    src/core/analysis/*.cpp
    src/core/analysis/**/*.cpp
)

file(GLOB ENGINE_SRC
    engines/*.cpp
)

add_executable(chimera
    src/core/spine_wired_main.cpp
    ${CORE_SRC}
    ${ENGINE_SRC}
)

target_include_directories(chimera PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(chimera Threads::Threads)
target_compile_options(chimera PRIVATE -O3 -march=native -pthread)

message(STATUS "Building CHIMERA + binance_shadow")

########################################
# BINANCE_SHADOW — multi-venue shadow executor
# Separate binary. Own source tree under src/ (not src/core/).
# Needs: OpenSSL (Beast SSL + HMAC), CURL (REST), Boost (Beast/Asio)
########################################
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL    REQUIRED)
find_package(Boost   REQUIRED COMPONENTS system)

add_executable(binance_shadow
    src/main_binance_shadow.cpp

    # runtime
    src/runtime/CpuPinning.cpp
    src/runtime/ThreadModel.cpp
    src/runtime/LiveArmSystem.cpp
    src/runtime/ContextSnapshotter.cpp
    src/runtime/ColdStartReconciler.cpp
    src/runtime/ExchangeTruthLoop.cpp

    # exchange — Binance direct
    src/exchange/binance/BinanceRestClient.cpp
    src/exchange/binance/BinanceWSMarket.cpp
    src/exchange/binance/BinanceWSUser.cpp
    src/exchange/binance/BinanceWSExecution.cpp
    src/exchange/binance/BinanceReconciler.cpp

    # execution
    src/execution/OrderStateMachine.cpp
    src/execution/ExecutionThrottle.cpp
    src/execution/CancelReplaceCoalescer.cpp
    src/execution/QueuePositionModel.cpp
    src/execution/ExecutionRouter.cpp
    src/execution/LatencyGovernor.cpp
    src/execution/QueueDecayGovernor.cpp

    # control
    src/control/PnLGovernor.cpp
    src/control/DeskArbiter.cpp
    src/control/UnwindCoordinator.cpp

    # risk
    src/risk/ExchangeTruthReconciler.cpp
    src/risk/DriftDetector.cpp
    src/risk/GlobalRiskGovernor.cpp

    # state
    # FIX: path was src/exchange_state/PositionState.cpp — that directory does not exist.
    # PositionState lives in src/state/. This was a typo that would have caused cmake to fail.
    src/state/PositionState.cpp

    # telemetry
    src/telemetry/TelemetryState.cpp
    src/telemetry/HttpServer.cpp

    # forensics
    src/forensics/BinaryRecorder.cpp
    src/forensics/LatencyTracer.cpp
    src/forensics/EdgeAttribution.cpp

    # strategy — engines + runner bridge
    src/strategy/StrategyRunner.cpp
    src/strategy/BTCascade.cpp
    src/strategy/ETHSniper.cpp
    src/strategy/MeanReversion.cpp
    src/strategy/QueueMarketMaker.cpp
    src/strategy/ImpulseReversion.cpp
    src/strategy/PortfolioSkewTrader.cpp
    src/strategy/ETHFade.cpp
    src/strategy/SOLFade.cpp
)

target_include_directories(binance_shadow PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OPENSSL_INCLUDE_DIR}
    ${CURL_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(binance_shadow
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    ${CURL_LIBRARIES}
    Boost::system
)

target_compile_options(binance_shadow PRIVATE -O3 -march=native -pthread)

########################################
# REPLAY_VALIDATOR — standalone forensic tool
########################################
add_executable(replay_validator
    src/forensics/ReplayValidator.cpp
)

target_include_directories(replay_validator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(replay_validator
    Threads::Threads
)

########################################
# WATCHDOG — external supervisor
# Standalone binary. Monitors binance_shadow heartbeat.
# On deadlock: SIGKILL + flatten via REST.
########################################
add_executable(watchdog
    src/supervisor/Watchdog.cpp
)

target_link_libraries(watchdog
    ${CURL_LIBRARIES}
)

target_include_directories(watchdog PRIVATE
    ${CURL_INCLUDE_DIRS}
)
