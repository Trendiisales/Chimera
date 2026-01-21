#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -e

FILE=main.cpp

echo "[CHIMERA] Wiring operator console into $FILE"

# Insert include at top if missing
grep -q 'gui/live_operator_server.cpp' "$FILE" || \
sed -i '1i #include "gui/live_operator_server.cpp"' "$FILE"

# Insert startup call inside main() if missing
grep -q 'start_operator_console' "$FILE" || \
sed -i '/int main(/a \ \ \ \ start_operator_console(8080);' "$FILE"

echo "[CHIMERA] Rebuilding"
cd build
make -j
