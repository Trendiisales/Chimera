#!/usr/bin/env bash
set -e

echo "[CHIMERA] Purging FlightDeckServer from build graph"

cp CMakeLists.txt CMakeLists.txt.bak

awk '
/telemetry\/FlightDeckServer\.cpp/ { next }
{ print }
' CMakeLists.txt.bak > CMakeLists.txt

echo "[CHIMERA] Done"
echo "[CHIMERA] Backup: CMakeLists.txt.bak"
