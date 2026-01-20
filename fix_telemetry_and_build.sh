#!/usr/bin/env bash
set -e

echo "[CHIMERA] FULL TELEMETRY REPAIR + REBUILD"

############################################
# BACKUP
############################################
cp main.cpp main.cpp.bak.$(date +%s)

############################################
# FIX STRAY \n IN MAIN.CPP
############################################
echo "[FIX] Cleaning stray newline artifacts in main.cpp"
sed -i 's/\\n[[:space:]]*startTelemetry();/    startTelemetry();/g' main.cpp

############################################
# ENSURE TELEMETRY DIRS
############################################
mkdir -p telemetry
mkdir -p telemetry/vendor

############################################
# INSTALL CPP-HTTPLIB (HEADER-ONLY)
############################################
if [ ! -f telemetry/vendor/httplib.h ]; then
  echo "[FETCH] Installing cpp-httplib"
  curl -L https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h \
    -o telemetry/vendor/httplib.h
fi

############################################
# PATCH TELEMETRY INCLUDES
############################################
echo "[PATCH] Fixing httplib include paths"

shopt -s nullglob
for f in telemetry/*.cpp telemetry/*.hpp; do
  sed -i 's|#include "external/httplib.h"|#include "vendor/httplib.h"|g' "$f"
done
shopt -u nullglob

############################################
# VERIFY STARTTELEMETRY PLACEMENT
############################################
if ! grep -q "startTelemetry();" main.cpp; then
  echo "[PATCH] Injecting startTelemetry()"

  awk '
    /int main/ {
      print
      print "    startTelemetry();"
      next
    }
    { print }
  ' main.cpp > main.cpp.tmp

  mv main.cpp.tmp main.cpp
fi

############################################
# REBUILD
############################################
echo "[CHIMERA] Rebuilding"
cd build
make -j$(nproc)

echo
echo "[CHIMERA] TELEMETRY FIXED + BUILT"
echo "OPEN GUI:"
echo "http://15.168.16.103:8080"
echo
