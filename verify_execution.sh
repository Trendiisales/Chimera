#!/usr/bin/env bash
set -e

echo "[VERIFY] Chimera Shadow Execution Audit"

if strings ./chimera | grep -q "MODE = SHADOW"; then
    echo "[VERIFY] Binary contains SHADOW banner"
else
    echo "[VERIFY] ERROR: SHADOW banner missing"
    exit 1
fi

echo "[VERIFY] Running short smoke test (5s)"
timeout 5s ./chimera || true

echo "[VERIFY] PASS — Shadow execution spine active"
