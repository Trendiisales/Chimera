#!/usr/bin/env bash
set -e
cd ~/chimera

############################################
# main.cpp — HEARTBEAT PRODUCER
############################################
cat << 'CPP' > main.cpp
#include <thread>
#include <iostream>
#include <chrono>

#include "telemetry/TelemetryBus.hpp"
#include "core/SymbolLane_ANTIPARALYSIS.hpp"
#include "execution/ShadowExecutor.hpp"

void runTelemetryServer();

int main() {
    std::cout << "[CHIMERA] MODE B LIVE STACK | SHADOW EXEC | TELEMETRY ACTIVE" << std::endl;

    std::thread telemetry_thread(runTelemetryServer);

    SymbolLane eth("ETH_PERP");
    SymbolLane btc("BTC_PERP");
    SymbolLane sol("SOL_SPOT");

    ShadowExecutor exec;

    double px = 1000.0;

    while (true) {
        tier3::TickData t{};
        px += 0.5;

        eth.onTick(t);
        btc.onTick(t);
        sol.onTick(t);

        exec.onIntent("FADE", "ETH_PERP", px, 1.0, 2.0);

        std::this_thread::sleep_for(std::chrono::seconds(30));
    }

    telemetry_thread.join();
    return 0;
}
CPP

############################################
# BUILD
############################################
rm -rf build
cmake -B build
cmake --build build -j
./build/chimera
