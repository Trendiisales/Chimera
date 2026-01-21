#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

echo "[CCP] Running Chimera Control Gate"

FAIL=0

echo "[CCP] Checking includes..."
while read -r line; do
  file=$(echo "$line" | cut -d: -f1)
  inc=$(echo "$line" | sed 's/.*#include "//;s/".*//')
  if [[ "$inc" == *"/"* ]]; then
    path="./$inc"
    if [ ! -f "$path" ]; then
      echo "ORPHAN INCLUDE: $file -> $inc"
      FAIL=1
    fi
  fi
done < .ccp_includes.txt

echo "[CCP] Checking CMake sources..."
grep -R "add_executable" -n CMakeLists.txt | while read -r line; do
  for token in $line; do
    if [[ "$token" == *.cpp ]]; then
      if [ ! -f "$token" ]; then
        echo "MISSING SOURCE: $token"
        FAIL=1
      fi
    fi
  done
done

if [ "$FAIL" -eq 1 ]; then
  echo
  echo "[CCP] BUILD BLOCKED — TREE IS INCONSISTENT"
  exit 1
fi

echo "[CCP] PASS — BUILD AUTHORIZED"
