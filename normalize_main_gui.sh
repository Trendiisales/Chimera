#!/usr/bin/env bash
set -e

echo "[CHIMERA] Normalizing GUI section in main.cpp"

cp main.cpp main.cpp.bak

# Remove old GUI includes
sed -i '/GuiServer/d' main.cpp
sed -i '/start_operator_console/d' main.cpp
sed -i '/telemetry\/FlightDeckServer/d' main.cpp
sed -i '/gui\/GuiServer/d' main.cpp

# Remove any old gui variable lines
sed -i '/GuiServer[[:space:]]\+gui/d' main.cpp
sed -i '/gui.start/d' main.cpp

# Remove duplicate new includes if present
sed -i '/gui_snapshot_bus.hpp/d' main.cpp
sed -i '/live_operator_server.hpp/d' main.cpp

# Insert correct headers at top
sed -i '1i #include "live_operator_server.hpp"' main.cpp
sed -i '1i #include "gui_snapshot_bus.hpp"' main.cpp

# Insert server start right after main() opening brace
awk '
BEGIN { done=0 }
/int[[:space:]]+main[[:space:]]*\(/ {
  print
  getline
  print
  print "    static LiveOperatorServer gui(8080);"
  print "    gui.start();"
  done=1
  next
}
{ print }
END {
  if(done==0) {
    print ""
    print "// GUI START (inserted at end because main() not detected)"
    print "static LiveOperatorServer gui(8080);"
    print "gui.start();"
  }
}
' main.cpp > main.cpp.tmp && mv main.cpp.tmp main.cpp

echo "[CHIMERA] main.cpp GUI normalized"
echo "[CHIMERA] Backup: main.cpp.bak"
