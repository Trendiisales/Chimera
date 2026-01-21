#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

ROOT="$HOME/chimera_work"
MAIN="$ROOT/main.cpp"
BUILD="$ROOT/build"

echo "[CHIMERA] Wiring FLOW -> GUI bridge"

if ! grep -q "gui_set_html" "$MAIN"; then
  echo "[CHIMERA] Injecting gui_set_html under FLOW print"

  awk '
  {
    print
    if ($0 ~ /std::cout << "\\[FLOW\\]"/) {
      print "    gui_set_html(flow_string);"
    }
  }' "$MAIN" > "$MAIN.tmp"

  mv "$MAIN.tmp" "$MAIN"
else
  echo "[CHIMERA] gui_set_html already wired"
fi

echo "[CHIMERA] Rebuilding"
cd "$BUILD"
make -j

echo
echo "[CHIMERA] STARTING"
./chimera
