#!/usr/bin/env bash
set -e

echo "[CHIMERA] Fixing main.cpp GUI includes"

cp main.cpp main.cpp.bak

sed -i '/gui\/live_operator_server\.cpp/d' main.cpp

if ! grep -q live_operator_server.hpp main.cpp; then
  sed -i '1i #include "live_operator_server.hpp"' main.cpp
fi

if ! grep -q gui_snapshot_bus.hpp main.cpp; then
  sed -i '1i #include "gui_snapshot_bus.hpp"' main.cpp
fi

echo "[CHIMERA] main.cpp fixed (backup: main.cpp.bak)"
