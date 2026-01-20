#include <thread>
#include <chrono>
#include <iostream>

#include "telemetry/TelemetryBus.hpp"
#include "core/SymbolLane_ANTIPARALYSIS.hpp"
#include "execution/ShadowExecutor.hpp"

using namespace std::chrono_literals;

int main() {
    std::cout << "[CHIMERA] MODE B LIVE STACK | SHADOW EXEC | TELEMETRY ACTIVE\n";

    // Lanes
    SymbolLane eth("ETH_PERP");
    SymbolLane btc("BTC_PERP");
    SymbolLane sol("SOL_SPOT");

    // Trade generator (shadow)
    ShadowExecutor shadow;

    // Register engines once
    TelemetryBus::instance().updateEngine({
        "ETH_PERP", 0.0, 0.0, 0, 0.0, 1.0, 1.0, "LIVE"
    });
    TelemetryBus::instance().updateEngine({
        "BTC_PERP", 0.0, 0.0, 0, 0.0, 1.0, 1.0, "LIVE"
    });
    TelemetryBus::instance().updateEngine({
        "SOL_SPOT", 0.0, 0.0, 0, 0.0, 1.0, 1.0, "LIVE"
    });

    while (true) {
        // Simulate a trade so telemetry is never empty
        shadow.onIntent(
            "FADE",
            "ETH_PERP",
            2.5,
            25.0
        );

        // Operator cadence, NOT tick cadence
        std::this_thread::sleep_for(30s);
    }
}
