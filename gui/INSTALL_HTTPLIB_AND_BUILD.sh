#!/usr/bin/env bash
set -e

echo "[CHIMERA] Installing cpp-httplib single header"

if [ ! -f gui/httplib.h ]; then
    curl -L https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h -o gui/httplib.h
    echo "[CHIMERA] httplib.h installed"
else
    echo "[CHIMERA] httplib.h already present"
fi

echo "[CHIMERA] Rebuilding"
cd build
make -j
echo "[CHIMERA] DONE"
