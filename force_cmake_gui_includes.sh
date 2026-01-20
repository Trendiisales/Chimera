#!/usr/bin/env bash
set -e

echo "[CHIMERA] Forcing gui/include into chimera target include paths"

cp CMakeLists.txt CMakeLists.txt.bak

awk '
BEGIN { injected=0 }

/add_executable[[:space:]]*\([[:space:]]*chimera/ {
  print
  print "target_include_directories(chimera PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui/include)"
  injected=1
  next
}

{ print }

END {
  if(injected==0) {
    print ""
    print "target_include_directories(chimera PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui/include)"
  }
}
' CMakeLists.txt.bak > CMakeLists.txt

echo "[CHIMERA] Done (backup: CMakeLists.txt.bak)"
