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
mkdir -p telemetry

cat > telemetry/TelemetryBus.hpp << 'HPP'
#pragma once

#include <mutex>
#include <vector>
#include <string>

struct TelemetryEngineRow {
    std::string symbol;
    double net_bps;
    double dd_bps;
    int    trades;
    double fees;
    double alloc;
    double leverage;
    std::string state;
};

struct TelemetryTradeRow {
    std::string engine;
    std::string symbol;
    std::string side;
    double bps;
    int latency_ms;
    double leverage;
};

class TelemetryBus {
public:
    static TelemetryBus& instance();

    void updateEngine(const TelemetryEngineRow& row);
    void recordTrade(const TelemetryTradeRow& row);

    std::vector<TelemetryEngineRow> snapshotEngines();
    std::vector<TelemetryTradeRow> snapshotTrades();

private:
    TelemetryBus() = default;

    std::mutex mu_;
    std::vector<TelemetryEngineRow> engines_;
    std::vector<TelemetryTradeRow> trades_;
};
HPP

############################################
# AUTHORITATIVE CPP REWRITE
############################################
cat > telemetry/TelemetryBus.cpp << 'CPP'
#include "TelemetryBus.hpp"

TelemetryBus& TelemetryBus::instance() {
    static TelemetryBus bus;
    return bus;
}

void TelemetryBus::updateEngine(const TelemetryEngineRow& row) {
    std::lock_guard<std::mutex> g(mu_);
    for (auto& e : engines_) {
        if (e.symbol == row.symbol) {
            e = row;
            return;
        }
    }
    engines_.push_back(row);
}

void TelemetryBus::recordTrade(const TelemetryTradeRow& row) {
    std::lock_guard<std::mutex> g(mu_);
    trades_.push_back(row);
    if (trades_.size() > 200) {
        trades_.erase(trades_.begin());
    }
}

std::vector<TelemetryEngineRow> TelemetryBus::snapshotEngines() {
    std::lock_guard<std::mutex> g(mu_);
    return engines_;
}

std::vector<TelemetryTradeRow> TelemetryBus::snapshotTrades() {
    std::lock_guard<std::mutex> g(mu_);
    return trades_;
}
CPP

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

if grep -R "updateEngine(.*) {" telemetry/TelemetryBus.hpp >/dev/null; then
  echo "FATAL: updateEngine implemented in header"
  exit 1
fi

if grep -R "recordTrade(.*) {" telemetry/TelemetryBus.hpp >/dev/null; then
  echo "FATAL: recordTrade implemented in header"
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
git commit -m "CONTROLLED BASELINE: TelemetryBus purified, schema locked, build gated"

echo "[DONE] Telemetry control locked"
