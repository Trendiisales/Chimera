#!/usr/bin/env bash
set -e

echo "[CHIMERA] FINAL GUARD FIX"

########################################
# REWRITE BUILD GUARD — FILTER SSOT PROPERLY
########################################
cat > build_guard.sh << 'GUARD'
#!/usr/bin/env bash
set -e

echo "[GUARD] Enforcing Chimera invariants..."

PATTERN="struct MarketTick|struct OrderIntent|struct FillEvent|class IEngine|class Spine|struct ChimeraTelemetry"

# Find matches, then REMOVE anything from core/contract.hpp
DUPS=$(grep -R -E "$PATTERN" . \
  --exclude-dir=build \
  --exclude="*.sh" | grep -v "core/contract.hpp" || true)

if [ -n "$DUPS" ]; then
  echo "[GUARD] DUPLICATE CORE TYPES DETECTED"
  echo "$DUPS"
  exit 1
fi

# Enforce compiled engine model
for H in engines/*.hpp; do
  [ -f "$H" ] || continue
  CPP="${H%.hpp}.cpp"
  if [ ! -f "$CPP" ]; then
    echo "[GUARD] ENGINE HEADER WITHOUT CPP: $H"
    exit 1
  fi
done

for C in engines/*.cpp; do
  [ -f "$C" ] || continue
  HPP="${C%.cpp}.hpp"
  if [ ! -f "$HPP" ]; then
    echo "[GUARD] ENGINE CPP WITHOUT HEADER: $C"
    exit 1
  fi
done

echo "[GUARD] OK"
GUARD

chmod +x build_guard.sh

########################################
# CLEAN + REBUILD
########################################
rm -rf build
mkdir build
./build.sh
