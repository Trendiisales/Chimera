#!/usr/bin/env bash
set -e

############################################
# CONFIG
############################################
ROOT_MARKER=".chimera_root"
TELEMETRY_LOCK="telemetry/.telemetry_contract.lock"
ENGINE_LOCK="core/.engine_registry.lock"

ROOT_GUARD='[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }'

############################################
# ROOT MARKER
############################################
echo "[CONTROL] Chimera Control Plane Initializing"

if [ ! -f "$ROOT_MARKER" ]; then
  echo "CHIMERA_CANONICAL_ROOT=1" > "$ROOT_MARKER"
  echo "[ROOT] Marker created"
else
  echo "[ROOT] Marker exists"
fi

############################################
# ROOT GUARD INJECTION
############################################
echo "[CONTROL] Enforcing root guard in all scripts"

find . -type f -name "*.sh" | while read -r file; do
  if ! grep -F "$ROOT_GUARD" "$file" >/dev/null; then
    echo "[PATCH] $file"

    tmp="$(mktemp)"
    {
      read -r first || true
      echo "$first"
      echo "$ROOT_GUARD"
      cat
    } < "$file" > "$tmp"

    chmod --reference="$file" "$tmp"
    mv "$tmp" "$file"
  else
    echo "[OK] $file"
  fi
done

############################################
# TELEMETRY CONTRACT LOCK
############################################
echo "[CONTROL] Locking telemetry contract"

mkdir -p telemetry

cat > "$TELEMETRY_LOCK" << 'LOCK'
TelemetryEngineRow:
  symbol
  net_bps
  dd_bps
  trades
  fees
  alloc
  leverage
  state

TelemetryTradeRow:
  engine
  symbol
  side
  bps
  latency_ms
  leverage

TelemetryBus API:
  updateEngine(TelemetryEngineRow)
  recordTrade(TelemetryTradeRow)
  snapshotEngines()
  snapshotTrades()
LOCK

############################################
# ENGINE REGISTRY LOCK
############################################
echo "[CONTROL] Locking engine registry"

mkdir -p core

cat > "$ENGINE_LOCK" << 'LOCK'
SYMBOLS:
  ETH_PERP
  BTC_PERP
  SOL_SPOT

ENGINES:
  FADE
  CASCADE
  FUNDING
  CROSS_SYMBOL

POLICY:
  - No symbol may be added without updating this file
  - No engine may emit telemetry unless registered here
LOCK

############################################
# BUILD GUARD SCRIPT
############################################
cat > CONTROL_BUILD.sh << 'BUILD'
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
BUILD

############################################
# RUN GUARD SCRIPT
############################################
cat > CONTROL_RUN.sh << 'RUN'
#!/usr/bin/env bash
set -e

[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

echo "[CONTROL] Runtime Guard Active"

if ! ss -lntp | grep -q ":8080"; then
  echo "[CHECK] Telemetry not running yet"
else
  echo "[CHECK] Telemetry port active"
fi

./build/chimera
RUN

############################################
# PERMS
############################################
chmod +x CHIMERA_CONTROL_PLANE.sh
chmod +x CONTROL_BUILD.sh
chmod +x CONTROL_RUN.sh

echo "[CONTROL] Chimera is now under enforced governance"
echo "[NEXT] Use:"
echo "  ./CONTROL_BUILD.sh"
echo "  ./CONTROL_RUN.sh"
