#!/usr/bin/env bash
set -e

echo "[CHIMERA] Removing FlightDeckServer from main.cpp"

cp main.cpp main.cpp.bak

# Remove any include of FlightDeckServer
sed -i '/FlightDeckServer.cpp/d' main.cpp
sed -i '/FlightDeckServer.hpp/d' main.cpp
sed -i '/telemetry\/FlightDeckServer/d' main.cpp

echo "[CHIMERA] Done"
echo "[CHIMERA] Backup saved as main.cpp.bak"
