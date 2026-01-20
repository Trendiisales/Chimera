#!/usr/bin/env bash
set -e

HPP=~/chimera_work/core/SymbolLane_ANTIPARALYSIS.hpp
MAIN=~/chimera_work/main.cpp

echo "[PATCH] Removing missing FundingSniper include..."
sed -i '/FundingSniper.hpp/d' "$HPP"

echo "[PATCH] Fixing TickData usage in main.cpp..."

# Remove set_midprice / set_ofi_z style calls
sed -i 's/tick\.set_midprice([^)]*);/tick.bid = bid; tick.ask = ask;/g' "$MAIN"
sed -i 's/tick\.set_ofi_z([^)]*);/tick.ofi_z = (bid_sz - ask_sz);/g' "$MAIN"

# Also fix any leftover midprice_/ofi_z_ assignments
sed -i 's/tick\.midprice_ *=.*/tick.bid = bid; tick.ask = ask;/g' "$MAIN"
sed -i 's/tick\.ofi_z_ *=.*/tick.ofi_z = (bid_sz - ask_sz);/g' "$MAIN"

echo "[PATCH] Done."
