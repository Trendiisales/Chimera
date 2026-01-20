#!/usr/bin/env bash
set -euo pipefail

############################################
# HARD ANCHOR
############################################
REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null || true)
if [ -z "$REPO_ROOT" ]; then
  echo "FATAL: Not inside git repo"
  exit 1
fi
cd "$REPO_ROOT"

echo "[CONTROL] Chimera Guardrail Active"
echo "[ROOT] $REPO_ROOT"

############################################
# AUTHORITATIVE HEADER REWRITE
############################################
mkdir -p core

cat > core/SymbolLane_ANTIPARALYSIS.hpp << 'HPP'
#pragma once

#include <string>
#include "../tier3/TickData.hpp"
#include "../telemetry/TelemetryBus.hpp"

class SymbolLane {
public:
    explicit SymbolLane(const std::string& sym);
    void onTick(const tier3::TickData& t);

private:
    std::string symbol_;

    double net_bps_;
    double dd_bps_;
    int    trade_count_;
    double fees_paid_;
    double alloc_;
    double leverage_;
};
HPP

############################################
# REQUIRED FILES
############################################
REQUIRED=(
  main.cpp
  telemetry/TelemetryBus.hpp
  telemetry/TelemetryBus.cpp
  telemetry/TelemetryServer.cpp
  execution/ShadowExecutor.hpp
  execution/ShadowExecutor.cpp
  core/SymbolLane_ANTIPARALYSIS.hpp
  core/SymbolLane_ANTIPARALYSIS.cpp
  allocator/CapitalAllocator.hpp
  ledger/TradeLedger.hpp
)

echo "[CHECK] Required files"
for f in "${REQUIRED[@]}"; do
  if [ ! -f "$f" ]; then
    echo "FATAL: Missing required file: $f"
    exit 1
  fi
done

############################################
# HEADER PURITY CHECK
############################################
echo "[CHECK] Header purity"

if grep -R "onTick(.*) {" core/SymbolLane_ANTIPARALYSIS.hpp >/dev/null; then
  echo "FATAL: onTick implemented in header"
  exit 1
fi

if grep -R "updateEngine(.*) {" telemetry/TelemetryBus.hpp >/dev/null; then
  echo "FATAL: updateEngine implemented in header"
  exit 1
fi

############################################
# CLEAN BUILD
############################################
echo "[BUILD] Clean build"
rm -rf build
cmake -B build
cmake --build build -j

############################################
# SMOKE RUN
############################################
echo "[RUN] Smoke test (3s)"
timeout 3 ./build/chimera || true

############################################
# COMMIT
############################################
echo "[GIT] Status"
git status --porcelain

git add .
git commit -m "CONTROLLED BASELINE: SymbolLane header purified, schema locked, build gated"

echo "[DONE] System locked"
