#!/usr/bin/env bash
set -e

echo "[CHIMERA] Wiring Telemetry (safe mode, no sed quoting)"

############################################
# BACKUPS
############################################
cp main.cpp main.cpp.bak.$(date +%s)
cp router/CapitalRouter.cpp router/CapitalRouter.cpp.bak.$(date +%s)
cp core/SymbolLane_ANTIPARALYSIS.cpp core/SymbolLane_ANTIPARALYSIS.cpp.bak.$(date +%s)

############################################
# MAIN.CPP
############################################

if ! grep -q "telemetry/telemetry_boot.hpp" main.cpp; then
  echo "[PATCH] main.cpp include"
  awk '
    NR==1 { print "#include \"telemetry/telemetry_boot.hpp\"" }
    { print }
  ' main.cpp > main.cpp.tmp
  mv main.cpp.tmp main.cpp
fi

if ! grep -q "startTelemetry()" main.cpp; then
  echo "[PATCH] main.cpp startup hook"
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
# CAPITAL ROUTER
############################################

if ! grep -q "TelemetryBus" router/CapitalRouter.cpp; then
  echo "[PATCH] CapitalRouter telemetry"

  awk '
    NR==1 { print "#include \"../telemetry/TelemetryBus.hpp\"" }
    { print }
  ' router/CapitalRouter.cpp > router/CapitalRouter.cpp.tmp

  mv router/CapitalRouter.cpp.tmp router/CapitalRouter.cpp

  awk '
    {
      print
      if ($0 ~ /rest_->sendOrder/) {
        print "        TelemetryBus::instance().recordTrade({"
        print "            \"ROUTER\","
        print "            symbol,"
        print "            is_buy ? \"BUY\" : \"SELL\","
        print "            qty,"
        print "            price,"
        print "            price,"
        print "            0.0,"
        print "            0.0,"
        print "            0.0,"
        print "            1.0,"
        print "            (uint64_t)time(nullptr)"
        print "        });"
      }
    }
  ' router/CapitalRouter.cpp > router/CapitalRouter.cpp.tmp

  mv router/CapitalRouter.cpp.tmp router/CapitalRouter.cpp
fi

############################################
# SYMBOL LANE HEARTBEAT
############################################

FILE="core/SymbolLane_ANTIPARALYSIS.cpp"

if [ -f "$FILE" ] && ! grep -q "TelemetryBus" "$FILE"; then
  echo "[PATCH] SymbolLane heartbeat"

  awk '
    NR==1 { print "#include \"../telemetry/TelemetryBus.hpp\"" }
    { print }
  ' "$FILE" > "$FILE.tmp"

  mv "$FILE.tmp" "$FILE"

  awk '
    {
      print
      if ($0 ~ /void SymbolLane::onTick/) {
        getline
        print $0
        print "    static auto last_pub = std::chrono::steady_clock::now();"
        print "    auto now_pub = std::chrono::steady_clock::now();"
        print "    if (now_pub - last_pub >= std::chrono::seconds(1)) {"
        print "        last_pub = now_pub;"
        print "        TelemetryBus::instance().updateEngine({"
        print "            symbol_,"
        print "            net_bps_,"
        print "            dd_bps_,"
        print "            trade_count_,"
        print "            fees_paid_,"
        print "            alloc_,"
        print "            leverage_,"
        print "            \"LIVE\""
        print "        });"
        print "    }"
      }
    }
  ' "$FILE" > "$FILE.tmp"

  mv "$FILE.tmp" "$FILE"
fi

############################################
# BUILD
############################################

echo "[CHIMERA] Rebuilding"
cd build
make -j$(nproc)

echo
echo "[CHIMERA] TELEMETRY WIRED SUCCESSFULLY"
echo "OPEN GUI:"
echo "http://15.168.16.103:8080"
echo
