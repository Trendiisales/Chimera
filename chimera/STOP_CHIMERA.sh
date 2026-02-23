#!/usr/bin/env bash
echo "[STOP] Killing Chimera..."
pkill -9 chimera || true
pkill -9 -f run_chimera || true
pkill -9 -f taskset || true
echo "[STOP] Done."
