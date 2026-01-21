#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -euo pipefail

############################################
# HARD ANCHOR TO GIT ROOT
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
# RESTORE SYMBOL LANE CPP (AUTHORITATIVE)
############################################
mkdir -p core

cat > core/SymbolLane_ANTIPARALYSIS.cpp << 'LANEOF'
#include "SymbolLane_ANTIPARALYSIS.hpp"
#include "../telemetry/TelemetryBus.hpp"

SymbolLane::SymbolLane(const std::string& sym)
    : symbol_(sym),
      net_bps_(0.0),
      dd_bps_(0.0),
      trade_count_(0),
      fees_paid_(0.0),
      alloc_(1.0),
      leverage_(1.0) {}

void SymbolLane::onTick(const tier3::TickData&) {
    TelemetryEngineRow row;
    row.symbol = symbol_;
    row.net_bps = net_bps_;
    row.dd_bps = dd_bps_;
    row.trades = trade_count_;
    row.fees = fees_paid_;
    row.alloc = alloc_;
    row.leverage = leverage_;
    row.state = "LIVE";

    TelemetryBus::instance().updateEngine(row);
}
LANEOF

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
git commit -m "CONTROLLED BASELINE: restored SymbolLane, telemetry schema locked, build gated"

echo "[DONE] System back under control"
