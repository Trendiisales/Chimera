#!/usr/bin/env bash
set -e

echo "[CHIMERA] Adding gui/include to chimera target include paths"

cp CMakeLists.txt CMakeLists.txt.bak

# If target_include_directories for chimera already exists, append to it
if grep -q "target_include_directories.*chimera" CMakeLists.txt; then
  sed -i "/target_include_directories.*chimera/ s#)$# gui/include)#" CMakeLists.txt
else
  # Otherwise, insert a new one after add_executable(chimera ...)
  sed -i "/add_executable.*chimera/a target_include_directories(chimera PRIVATE gui/include)" CMakeLists.txt
fi

echo "[CHIMERA] Done (backup: CMakeLists.txt.bak)"
