#!/usr/bin/env bash
set -e

HPP=~/chimera_work/core/SymbolLane_ANTIPARALYSIS.hpp
CPP=~/chimera_work/core/SymbolLane_ANTIPARALYSIS.cpp

echo "[PATCH] Updating include to EngineControlServer..."

sed -i 's|#include "../metrics/EngineMetricsServer.hpp"|#include "../metrics/EngineControlServer.hpp"|g' "$HPP"
sed -i 's|#include "../metrics/EngineMetricsServer.hpp"|#include "../metrics/EngineControlServer.hpp"|g' "$CPP"

echo "[PATCH] Updating constructor to use EngineControlServer..."

sed -i 's|std::make_unique<EngineMetricsServer>|std::make_unique<EngineControlServer>|g' "$CPP"

echo "[PATCH] Done."
