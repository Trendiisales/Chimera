#!/usr/bin/env bash
set -e

echo "[CHIMERA] Fixing root CMakeLists.txt (removing dead GUI target)"

if ! grep -q chimera_operator CMakeLists.txt; then
  echo "[CHIMERA] No chimera_operator target found — nothing to fix"
  exit 0
fi

cp CMakeLists.txt CMakeLists.txt.bak

awk '
BEGIN { skip=0 }
/add_executable[[:space:]]*\([[:space:]]*chimera_operator/ { skip=1; next }
/\)/ { if(skip==1) { skip=0; next } }
{ if(skip==0) print }
' CMakeLists.txt.bak > CMakeLists.txt

echo "[CHIMERA] Removed chimera_operator target"
echo "[CHIMERA] Backup saved as CMakeLists.txt.bak"
