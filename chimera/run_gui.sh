#!/bin/bash
if pgrep -f chimera_gui >/dev/null; then
  echo "[LOCK] GUI already running"
  exit 1
fi
cd "$(dirname "$0")"
exec ./build/chimera_gui
