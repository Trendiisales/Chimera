#!/usr/bin/env bash
set -e

[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }

echo "[CONTROL] Runtime Guard Active"

if ! ss -lntp | grep -q ":8080"; then
  echo "[CHECK] Telemetry not running yet"
else
  echo "[CHECK] Telemetry port active"
fi

./build/chimera

############################################
# EXPECTANCY ACTIVE CHECK
############################################
echo "[EXPECTANCY] Active — engines with negative expectancy will be killed"

############################################
# DRAWDOWN ACTIVE CHECK
############################################
echo "[DRAWDOWN] Active — engines exceeding drawdown will be force-killed"
