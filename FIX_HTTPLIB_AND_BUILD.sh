#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

echo "[CHIMERA] FIXING HTTPLIB DEPENDENCY"

############################################
# VENDOR HTTPLIB
############################################
mkdir -p telemetry/vendor

if [ ! -f telemetry/vendor/httplib.h ]; then
    echo "[CHIMERA] Downloading cpp-httplib"
    curl -L https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h \
        -o telemetry/vendor/httplib.h
fi

############################################
# PATCH INCLUDE PATH
############################################
echo "[CHIMERA] Patching FlightDeckServer include"

sed -i 's|#include "external/httplib.h"|#include "vendor/httplib.h"|g' telemetry/FlightDeckServer.cpp

############################################
# REBUILD
############################################
echo "[CHIMERA] Rebuilding"
cd build
make clean
make -j$(nproc)

############################################
# RUN
############################################
echo
echo "[CHIMERA] STARTING FLIGHT DECK"
./chimera
