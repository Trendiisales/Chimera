#!/usr/bin/env bash
set -e

ROOT="/home/trader/Chimera"
BIN="$ROOT/build/chimera"
ENV="$ROOT/cpu_pinning.env"

source "$ENV"

echo "[RUN] Single-process Chimera with CPU pinning"
echo "[RUN] Replacing shell with Chimera (Ctrl-C safe)"
echo "[RUN] CPU cores: $FIX_TRADE,$ENGINES"

# Replace THIS shell with chimera
exec taskset -c "$FIX_TRADE,$ENGINES" "$BIN"
