#!/usr/bin/env bash
set -e

[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

echo "[CONTROL] Build Guard Active"

############################################
# REQUIRED FILES
############################################
REQUIRED=(
telemetry/TelemetryBus.hpp
telemetry/TelemetryBus.cpp
telemetry/TelemetryServer.cpp
core/SymbolLane_ANTIPARALYSIS.hpp
core/SymbolLane_ANTIPARALYSIS.cpp
execution/ShadowExecutor.cpp
)

for f in "${REQUIRED[@]}"; do
  if [ ! -f "$f" ]; then
    echo "FATAL: Missing required file: $f"
    exit 1
  fi
done

############################################
# HEADER PURITY CHECK
############################################
if grep -R "updateEngine(.*) {" telemetry/TelemetryBus.hpp >/dev/null; then
  echo "FATAL: updateEngine implemented in header"
  exit 1
fi

if grep -R "recordTrade(.*) {" telemetry/TelemetryBus.hpp >/dev/null; then
  echo "FATAL: recordTrade implemented in header"
  exit 1
fi

############################################
# TELEMETRY CONTRACT CHECK
############################################
echo "[CONTROL] Verifying telemetry schema"

grep -q "struct TelemetryEngineRow" telemetry/TelemetryBus.hpp || {
  echo "FATAL: TelemetryEngineRow missing"
  exit 1
}

grep -q "struct TelemetryTradeRow" telemetry/TelemetryBus.hpp || {
  echo "FATAL: TelemetryTradeRow missing"
  exit 1
}

############################################
# ENGINE REGISTRY CHECK
############################################
echo "[CONTROL] Verifying engine registry"

while read -r sym; do
  [[ "$sym" =~ ^[A-Z_] ]] || continue
  if ! grep -R "$sym" core >/dev/null; then
    echo "FATAL: Registered symbol $sym not referenced in core/"
    exit 1
  fi
done < core/.engine_registry.lock

############################################
# BUILD
############################################
echo "[CONTROL] Clean build"

rm -rf build
cmake -B build
cmake --build build -j

echo "[CONTROL] Build OK"

############################################
# REGIME TRUTH CHECK
############################################
[ -f regime/MarketRegime.hpp ] || { echo "FATAL: Regime layer missing"; exit 1; }

############################################
# CAPITAL GOVERNANCE CHECK
############################################
[ -f governance/CapitalGovernor.hpp ] || { echo "FATAL: Capital layer missing"; exit 1; }

############################################
# EXPECTANCY JUDGE CHECK
############################################
[ -f expectancy/ExpectancyJudge.hpp ] || { echo "FATAL: Expectancy layer missing"; exit 1; }

############################################
# DRAWDOWN SENTINEL CHECK
############################################
[ -f drawdown/DrawdownSentinel.hpp ] || { echo "FATAL: Drawdown layer missing"; exit 1; }
