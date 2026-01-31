cmake_minimum_required(VERSION 3.16)
project(chimera LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
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
        message(FATAL_ERROR "MISSING REQUIRED FILE: ${f}")
    endif()
endforeach()

########################################
# COLLECT SOURCES
########################################
file(GLOB_RECURSE CORE_SRC
    src/core/state/*.cpp
    src/core/control/*.cpp
    src/core/execution/*.cpp
    src/core/latency/*.cpp
    src/core/replay/*.cpp
    src/core/gui/*.cpp
)

file(GLOB ENGINE_SRC
    engines/*.cpp
)

add_executable(chimera
    src/spine_wired_main.cpp
    ${CORE_SRC}
    ${ENGINE_SRC}
)

message(STATUS "Building CHIMERA from src/ (core/ is ignored)")
