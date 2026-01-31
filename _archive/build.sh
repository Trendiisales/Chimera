#!/usr/bin/env bash
set -e
./build_guard.sh
cd build
cmake ..
make -j
echo "[CHIMERA] BUILD COMPLETE"
./chimera
