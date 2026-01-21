#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

echo "[CHIMERA] Installing vendored httplib"

mkdir -p external
cd external

if [ ! -f httplib.h ]; then
  curl -L https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h -o httplib.h
fi

cd ..

echo "[CHIMERA] Patching all httplib includes to use vendored copy"

grep -rl '#include "external/httplib.h"' . | while read -r file; do
  sed -i 's|#include "external/httplib.h"|#include "external/httplib.h"|g' "$file"
  echo "[PATCHED] $file"
done

echo "[CHIMERA] Rebuilding"
cd build
make -j

echo
echo "[CHIMERA] DONE"
echo "Run: ./chimera"
echo "Open: http://15.168.16.103:8080"
