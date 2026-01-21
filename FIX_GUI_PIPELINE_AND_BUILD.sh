#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

echo "[CHIMERA] Installing GUI live feed pipeline"

ROOT="$HOME/chimera_work"
GUI="$ROOT/gui"
BUILD="$ROOT/build"

#######################################
# 1. CREATE GUI FEED HEADER
#######################################
mkdir -p "$GUI"

cat > "$GUI/gui_feed.hpp" << 'EOC'
#pragma once
#include <string>
#include <mutex>

static std::string GUI_LIVE_DATA;
static std::mutex GUI_LIVE_MUTEX;

inline void gui_set_html(const std::string& s) {
    std::lock_guard<std::mutex> lock(GUI_LIVE_MUTEX);
    GUI_LIVE_DATA = s;
}

inline std::string gui_get_html() {
    std::lock_guard<std::mutex> lock(GUI_LIVE_MUTEX);
    return GUI_LIVE_DATA;
}
EOC

echo "[CHIMERA] gui_feed.hpp installed"

#######################################
# 2. PATCH OPERATOR SERVER
#######################################
SERVER="$GUI/live_operator_server.cpp"

if ! grep -q gui_get_html "$SERVER"; then
    echo "[CHIMERA] Patching operator server"

    sed -i '1i #include "gui_feed.hpp"' "$SERVER"

    sed -i 's|<pre></pre>|<pre>" + gui_get_html() + "</pre>|g' "$SERVER"
else
    echo "[CHIMERA] Operator server already patched"
fi

#######################################
# 3. PATCH FLOW PRINT IN MAIN
#######################################
MAIN="$ROOT/main.cpp"

if ! grep -q gui_set_html "$MAIN"; then
    echo "[CHIMERA] Injecting gui_set_html into FLOW output"

    sed -i '/\\[FLOW\\]/a\\
    gui_set_html(flow_string);
' "$MAIN"
else
    echo "[CHIMERA] FLOW already wired"
fi

#######################################
# 4. REBUILD
#######################################
echo "[CHIMERA] Rebuilding"
cd "$BUILD"
make -j

#######################################
# 5. RUN
#######################################
echo "[CHIMERA] DONE"
echo "Run:"
echo "  ./chimera"
echo
echo "GUI:"
echo "  http://15.168.16.103:8080"
