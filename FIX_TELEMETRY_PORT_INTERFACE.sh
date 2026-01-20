#!/usr/bin/env bash
set -e

ROOT="$HOME/chimera_work"

echo "[CHIMERA] Locating startTelemetry implementation..."

IMPL=$(grep -R "void startTelemetry" -n "$ROOT" | grep -v build | head -n1 | cut -d: -f1)

if [ -z "$IMPL" ]; then
  echo "[ERROR] Could not find startTelemetry implementation"
  exit 1
fi

echo "[CHIMERA] Found implementation in: $IMPL"

HDR="$ROOT/telemetry/telemetry_boot.hpp"

if [ ! -f "$HDR" ]; then
  echo "[ERROR] telemetry_boot.hpp not found"
  exit 1
fi

echo "[CHIMERA] Patching header"
sed -i 's/void startTelemetry();/void startTelemetry(int port);/g' "$HDR"

echo "[CHIMERA] Patching implementation signature"
sed -i 's/void startTelemetry()/void startTelemetry(int port)/g' "$IMPL"

echo "[CHIMERA] Patching listen bind"
sed -i 's/listen([^)]*)/listen("0.0.0.0", port)/g' "$IMPL"

echo "[CHIMERA] Rebuilding"
cd "$ROOT/build"
make -j

echo "[CHIMERA] Starting Chimera"
./chimera
