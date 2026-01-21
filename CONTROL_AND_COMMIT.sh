#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -euo pipefail

############################################
# HARD ANCHOR TO GIT ROOT
############################################
if ! command -v git >/dev/null; then
  echo "FATAL: git not installed"
  exit 1
fi

REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null || true)
if [ -z "$REPO_ROOT" ]; then
  echo "FATAL: Not inside a git repository"
  exit 1
fi

cd "$REPO_ROOT"

echo "[CONTROL] Chimera Guardrail Active"
echo "[ROOT] $REPO_ROOT"

############################################
# FORCE TELEMETRY BUS CPP (AUTHORITATIVE)
############################################
mkdir -p telemetry

cat > telemetry/TelemetryBus.cpp << 'TELEOF'
#include "TelemetryBus.hpp"
#include <mutex>

TelemetryBus& TelemetryBus::instance() {
    static TelemetryBus bus;
    return bus;
}

void TelemetryBus::updateEngine(const TelemetryEngineRow& row) {
    std::lock_guard<std::mutex> g(mu_);
    bool found = false;
    for (auto& e : engines_) {
        if (e.symbol == row.symbol) {
            e = row;
            found = true;
            break;
        }
    }
    if (!found) {
        engines_.push_back(row);
    }
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
TELEOF

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
# TELEMETRY SCHEMA LOCK
############################################
echo "[CHECK] Telemetry schema"

grep -q "struct TelemetryEngineRow" telemetry/TelemetryBus.hpp || {
  echo "FATAL: TelemetryEngineRow missing"
  exit 1
}

grep -q "struct TelemetryTradeRow" telemetry/TelemetryBus.hpp || {
  echo "FATAL: TelemetryTradeRow missing"
  exit 1
}

############################################
# HEADER PURITY CHECK
############################################
echo "[CHECK] Headers must not contain implementations"

if grep -R "updateEngine(.*) {" telemetry/TelemetryBus.hpp >/dev/null; then
  echo "FATAL: updateEngine implemented in header"
  exit 1
fi

if grep -R "recordTrade(.*) {" telemetry/TelemetryBus.hpp >/dev/null; then
  echo "FATAL: recordTrade implemented in header"
  exit 1
fi

if grep -R "onTick(.*) {" core/SymbolLane_ANTIPARALYSIS.hpp >/dev/null; then
  echo "FATAL: onTick implemented in header"
  exit 1
fi

############################################
# INCLUDE SANITY
############################################
echo "[CHECK] main.cpp includes"

grep -q "telemetry/TelemetryBus.hpp" main.cpp || {
  echo "FATAL: main.cpp missing telemetry/TelemetryBus.hpp"
  exit 1
}

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
echo "[RUN] Smoke test (3 seconds)"
timeout 3 ./build/chimera || true

############################################
# COMMIT GATE
############################################
echo "[GIT] Status"
git status --porcelain

echo "[GIT] Committing controlled baseline"
git add .
git commit -m "CONTROLLED BASELINE: telemetry bus restored, schema locked, headers clean, build gated"

echo "[DONE] System is now under control"
