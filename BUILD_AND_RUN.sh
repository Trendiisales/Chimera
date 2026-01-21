#!/usr/bin/env bash
set -e

[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

telemetry/TELEMETRY_CONTRACT_CHECK.sh

rm -rf build
mkdir build
cd build

cmake ..
cmake --build . -j

./chimera
