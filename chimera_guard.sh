#!/usr/bin/env bash
set -e

echo "[CHIMERA GUARD] Checking for ODR violations..."

FAIL=0

for h in engines/*.hpp core/*.hpp gui/*.hpp; do
  [ -f "$h" ] || continue
  base=$(basename "$h" .hpp)
  cpp="engines/$base.cpp"
  cpp2="core/$base.cpp"
  cpp3="gui/$base.cpp"

  if [ -f "$cpp" ] || [ -f "$cpp2" ] || [ -f "$cpp3" ]; then
    echo
    echo "[ODR VIOLATION]"
    echo "Header: $h"
    echo "Matching .cpp found"
    [ -f "$cpp" ] && echo " - $cpp"
    [ -f "$cpp2" ] && echo " - $cpp2"
    [ -f "$cpp3" ] && echo " - $cpp3"
    FAIL=1
  fi
done

if [ "$FAIL" -eq 1 ]; then
  echo
  echo "[CHIMERA GUARD] BUILD BLOCKED — FIX ODR VIOLATIONS"
  exit 1
fi

echo "[CHIMERA GUARD] Clean"
