#!/usr/bin/env bash
set -e

ROOT="$HOME/chimera_work"
MAIN="$ROOT/main.cpp"
BUILD="$ROOT/build"

echo "[CHIMERA] Fixing FLOW injection (awk-safe)"

if grep -q "gui_set_html(flow_string)" "$MAIN"; then
    echo "[CHIMERA] FLOW already wired"
else
    awk '
    {
        print
        if ($0 ~ /\\[FLOW\\]/) {
            print "    gui_set_html(flow_string);"
        }
    }
    ' "$MAIN" > "$MAIN.tmp"
    mv "$MAIN.tmp" "$MAIN"
    echo "[CHIMERA] FLOW injection complete"
fi

#######################################
# REBUILD
#######################################
echo "[CHIMERA] Rebuilding"
cd "$BUILD"
make -j

#######################################
# DONE
#######################################
echo
echo "[CHIMERA] DONE"
echo "Run:"
echo "  ./chimera"
echo
echo "GUI:"
echo "  http://15.168.16.103:8080"
