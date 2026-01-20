#!/usr/bin/env bash
set -e

ROOT="$HOME/chimera_work"

echo "[CHIMERA] Fixing TelemetryBus API mismatch"
echo "[CHIMERA] Converting publish() -> push()"

FILES=(
  "$ROOT/learning/AutoKillLearner.hpp"
  "$ROOT/allocator/CapitalRotationAI.hpp"
  "$ROOT/learning/MonteCarloRisk.hpp"
  "$ROOT/main.cpp"
)

for f in "${FILES[@]}"; do
  if [ -f "$f" ]; then
    sed -i 's/TelemetryBus::instance().publish/TelemetryBus::instance().push/g' "$f"
    echo "[PATCHED] $f"
  fi
done

echo "[CHIMERA] Rebuilding"
cd "$ROOT/build"
make -j

echo
echo "[CHIMERA] DONE"
echo "Run:"
echo "./chimera"
echo
echo "GUI:"
echo "http://15.168.16.103:8080"
