#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e
if [ ! -f .chimera_root ]; then
  echo "FATAL: Not in canonical Chimera root"
  exit 1
fi
