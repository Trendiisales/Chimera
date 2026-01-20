#!/usr/bin/env bash
set -e

HPP=~/chimera_work/core/SymbolLane_ANTIPARALYSIS.hpp
CPP=~/chimera_work/core/SymbolLane_ANTIPARALYSIS.cpp

echo "[LEAN] Removing metrics system from SymbolLane..."

# Remove include
sed -i '/EngineControlServer.hpp/d' "$HPP"

# Remove forward declaration
sed -i '/class MetricsHTTPServer;/d' "$HPP"

# Remove member
sed -i '/metrics_server_/d' "$HPP"

# Remove constructor parameter (header)
sed -i 's/, *MetricsHTTPServer\* metrics_server//g' "$HPP"

# Remove constructor parameter (cpp)
sed -i 's/, *MetricsHTTPServer\* metrics_server//g' "$CPP"

# Remove assignment lines
sed -i '/metrics_server_/d' "$CPP"

echo "[LEAN] SymbolLane now core-only."
