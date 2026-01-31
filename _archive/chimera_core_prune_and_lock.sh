#!/usr/bin/env bash
set -e

echo "[CHIMERA] PRUNING LEGACY CORE + LOCKING BUILD SET"

########################################
# HARD DELETE ALL NON-FLAT CORE FILES
########################################
rm -f core/counterfactual.cpp || true
rm -f core/event_bus.cpp || true
rm -f core/capital_governor.cpp || true
rm -f core/event_spine.cpp || true
rm -f core/execution_router.cpp || true

########################################
# REWRITE CMAKE — NO GLOBS, EXPLICIT FILE LIST
########################################
cat > CMakeLists.txt << 'CMAKE'
cmake_minimum_required(VERSION 3.16)
project(chimera)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_custom_target(guard
    COMMAND ${CMAKE_SOURCE_DIR}/build_guard.sh
)

add_executable(chimera
    main.cpp
    core/spine.cpp
    engines/BTCascade.cpp
    engines/ETHSniper.cpp
    engines/MeanReversion.cpp
)

add_dependencies(chimera guard)
CMAKE

########################################
# CLEAN BUILD DIR
########################################
rm -rf build
mkdir build

########################################
# BUILD
########################################
./build.sh
