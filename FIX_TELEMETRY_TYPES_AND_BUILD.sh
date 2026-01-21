#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

ROOT=~/chimera_work
TEL="$ROOT/telemetry/TelemetryBus.hpp"

echo "[CHIMERA] Fixing Telemetry type compatibility (double -> string bridge)"

if [ ! -f "$TEL" ]; then
  echo "[ERROR] TelemetryBus.hpp not found at $TEL"
  exit 1
fi

# Remove any existing publish() wrappers
sed -i '/void publish(/,/}/d' "$TEL"

# Inject correct wrapper inside class
sed -i '/class TelemetryBus {/a \
    void publish(const std::string& type, const std::map<std::string, double>& fields) {\\\n\
        std::map<std::string, std::string> out;\\\n\
        for (const auto& kv : fields) {\\\n\
            out[kv.first] = std::to_string(kv.second);\\\n\
        }\\\n\
        push(type, out);\\\n\
    }\\\n' "$TEL"

echo "[CHIMERA] Telemetry API bridged"
echo "[CHIMERA] Rebuilding clean"

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
