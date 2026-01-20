#!/usr/bin/env bash
set -e

HPP=~/chimera_work/core/SymbolLane_ANTIPARALYSIS.hpp
MAIN=~/chimera_work/main.cpp

echo "[PATCH] Removing missing KillSwitchGovernor include..."
sed -i '/KillSwitchGovernor.hpp/d' "$HPP"

echo "[PATCH] Fixing bid/ask variables in main.cpp..."

# Ensure bid/ask exist in lambda or loop
# Insert default bid/ask if missing
sed -i '/TickData tick;/a\
        double bid = 0.0;\
        double ask = 0.0;\
        double bid_sz = 0.0;\
        double ask_sz = 0.0;' "$MAIN"

# Fix assignments safely
sed -i 's/tick\.bid = bid; tick\.ask = ask;/tick.bid = bid; tick.ask = ask;/g' "$MAIN"
sed -i 's/tick\.ofi_z = (bid_sz - ask_sz);/tick.ofi_z = (bid_sz - ask_sz);/g' "$MAIN"

echo "[PATCH] Done."
