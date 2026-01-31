#!/usr/bin/env bash
set -e

echo "[CHIMERA] Fixing Spine duplicate definition"

########################################
# DELETE DUPLICATE IMPLEMENTATION
########################################
if [ -f core/spine.cpp ]; then
  echo "[CHIMERA] Removing core/spine.cpp"
  rm -f core/spine.cpp
fi

########################################
# PATCH CMAKE
########################################
if grep -q "core/spine.cpp" CMakeLists.txt; then
  echo "[CHIMERA] Removing spine.cpp from CMake"
  sed -i 's|core/spine.cpp||g' CMakeLists.txt
fi

########################################
# CLEAN + REBUILD
########################################
rm -rf build
mkdir build
cd build

cmake ..
make -j$(nproc)

echo "[CHIMERA] SPINE FIXED — BUILD CLEAN"
