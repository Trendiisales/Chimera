#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

echo "[CHIMERA] Fixing startTelemetry() call"

ROOT="$HOME/chimera_work"
MAIN="$ROOT/main.cpp"

if ! grep -q "startTelemetry(" "$MAIN"; then
  echo "[ERROR] startTelemetry not found in main.cpp"
  exit 1
fi

# Replace zero-arg call with ported call
sed -i 's/startTelemetry();/startTelemetry(9090);/g' "$MAIN"

echo "[OK] Patched main.cpp"

echo "[CHIMERA] Rebuilding"
cd "$ROOT/build"
make -j

echo "[CHIMERA] Starting Chimera"
./chimera
