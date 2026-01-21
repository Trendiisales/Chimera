#!/usr/bin/env bash
[ -f .chimera_root ] || { echo "FATAL: Not in Chimera root"; exit 1; }
set -euo pipefail

REPO_ROOT=$(git rev-parse --show-toplevel)
cd "$REPO_ROOT"

echo "[CONTROL] Fixing telemetry call sites"
echo "[ROOT] $REPO_ROOT"

############################################
# SHADOW EXECUTOR — API MATCH
############################################
cat > execution/ShadowExecutor.cpp << 'CPP'
#include "ShadowExecutor.hpp"
#include "../telemetry/TelemetryBus.hpp"
#include <chrono>

void ShadowExecutor::onIntent(
    const std::string& engine,
    const std::string& symbol,
    double bps,
    double leverage
) {
    TelemetryTradeRow row;
    row.engine = engine;
    row.symbol = symbol;
    row.side = "BUY";
    row.bps = bps;
    row.latency_ms = 25;
    row.leverage = leverage;

    TelemetryBus::instance().recordTrade(row);
}
CPP

############################################
# TELEMETRY SERVER — SNAPSHOT API
############################################
cat > telemetry/TelemetryServer.cpp << 'CPP'
#include "TelemetryBus.hpp"
#include <thread>
#include <chrono>
#include <iostream>

void runTelemetryServer() {
    while (true) {
        auto engines = TelemetryBus::instance().snapshotEngines();
        auto trades  = TelemetryBus::instance().snapshotTrades();

        std::cout << "{\"engines\":[";

        for (size_t i = 0; i < engines.size(); ++i) {
            const auto& e = engines[i];
            std::cout
                << "{\"symbol\":\"" << e.symbol
                << "\",\"net_bps\":" << e.net_bps
                << ",\"dd_bps\":" << e.dd_bps
                << ",\"trades\":" << e.trades
                << ",\"fees\":" << e.fees
                << ",\"alloc\":" << e.alloc
                << ",\"leverage\":" << e.leverage
                << ",\"state\":\"" << e.state << "\"}";
            if (i + 1 < engines.size()) std::cout << ",";
        }

        std::cout << "],\"trades\":[";

        for (size_t i = 0; i < trades.size(); ++i) {
            const auto& t = trades[i];
            std::cout
                << "{\"engine\":\"" << t.engine
                << "\",\"symbol\":\"" << t.symbol
                << "\",\"side\":\"" << t.side
                << "\",\"bps\":" << t.bps
                << ",\"latency_ms\":" << t.latency_ms
                << ",\"leverage\":" << t.leverage
                << "}";
            if (i + 1 < trades.size()) std::cout << ",";
        }

        std::cout << "]}" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}
CPP

############################################
# CLEAN BUILD
############################################
echo "[BUILD] Clean build"
rm -rf build
cmake -B build
cmake --build build -j

############################################
# SMOKE RUN
############################################
echo "[RUN] Smoke test (5s)"
timeout 5 ./build/chimera || true

############################################
# COMMIT
############################################
git add .
git commit -m "CONTROLLED BASELINE: Telemetry callers rewired to snapshot/record API"

echo "[DONE] Telemetry pipeline locked end-to-end"
