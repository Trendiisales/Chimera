#!/usr/bin/env bash
set -e

echo "[CHIMERA] Cleaning legacy GUI includes in main.cpp"

cp main.cpp main.cpp.bak

# Remove any old GUI includes
sed -i '/GuiServer.hpp/d' main.cpp
sed -i '/gui\/GuiServer.hpp/d' main.cpp
sed -i '/gui\/live_operator_server.cpp/d' main.cpp
sed -i '/gui_snapshot_bus.hpp/d' main.cpp
sed -i '/live_operator_server.hpp/d' main.cpp

# Insert correct headers at top
sed -i '1i #include "gui_snapshot_bus.hpp"' main.cpp
sed -i '1i #include "live_operator_server.hpp"' main.cpp

echo "[CHIMERA] main.cpp fixed"
echo "[CHIMERA] Backup: main.cpp.bak"
