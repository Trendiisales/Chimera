#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

echo "[CHIMERA] Purging all chimera_operator references from CMakeLists.txt"

if ! grep -q chimera_operator CMakeLists.txt; then
  echo "[CHIMERA] No chimera_operator references found — nothing to do"
  exit 0
fi

cp CMakeLists.txt CMakeLists.txt.bak

awk '
BEGIN { skip=0 }

/add_executable[[:space:]]*\([[:space:]]*chimera_operator/ { skip=1; next }
/target_link_libraries[[:space:]]*\([[:space:]]*chimera_operator/ { skip=1; next }

/\)/ {
  if(skip==1) {
    skip=0
    next
  }
}

{
  if(skip==0) print
}
' CMakeLists.txt.bak > CMakeLists.txt

echo "[CHIMERA] chimera_operator fully removed"
echo "[CHIMERA] Backup: CMakeLists.txt.bak"
