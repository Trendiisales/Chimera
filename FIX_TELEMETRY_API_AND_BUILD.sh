#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

ROOT=~/chimera_work
TEL="$ROOT/telemetry/TelemetryBus.hpp"

echo "[CHIMERA] Locking Telemetry API (publish -> push wrapper)"

if [ ! -f "$TEL" ]; then
  echo "[ERROR] TelemetryBus.hpp not found at $TEL"
  exit 1
fi

# Remove any old publish() declarations to avoid duplicates
sed -i '/void publish(/,/}/d' "$TEL"

# Inject wrapper inside class before closing brace
sed -i '/class TelemetryBus {/a \
    void publish(const std::string& type, const std::map<std::string, double>& fields) {\\\n\
        push(type, fields);\\\n\
    }\\\n' "$TEL"

echo "[CHIMERA] Telemetry API locked"
echo "[CHIMERA] Rebuilding"

cd "$ROOT"
rm -rf build
mkdir build
cd build
cmake ..
make -j

echo
echo "[CHIMERA] DONE"
echo "Run:"
echo "./chimera"
echo
echo "GUI:"
echo "http://15.168.16.103:8080"
