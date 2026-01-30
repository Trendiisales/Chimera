#include <iostream>
#include <thread>
#include <chrono>

#include "core/ChimeraTelemetry.hpp"
#include "core/spine_runtime.hpp"
#include "GUIServer.hpp"

int main() {
    std::cout << "========================================\n";
    std::cout << "[CHIMERA] BOOT\n";
    std::cout << "[CHIMERA] MODE = SHADOW\n";
    std::cout << "========================================\n";

    mkdir("runs", 0755);

    SpineRuntime spine("runs/chimera.bin", "runs/chimera.jsonl");

    ChimeraTelemetry telemetry;
    telemetry.is_online = true;
    telemetry.is_trading = false;
    telemetry.regime = "INIT";

    GUIServer gui(12701, telemetry);
    gui.start();

    std::cout << "[CHIMERA] GUI STARTED\n";
    std::cout << "[CHIMERA] ENTERING SHADOW LOOP\n";

    uint64_t tick = 0;

    while (true) {
        tick++;

        telemetry.eth_price = telemetry.eth_price.load() + 0.01;
        telemetry.sol_price = telemetry.sol_price.load() + 0.02;

        telemetry.eth_trades++;
        telemetry.sol_trades++;

        std::string event = std::string("{\"tick\":") +
                            std::to_string(tick) +
                            ",\"eth_price\":" +
                            std::to_string(telemetry.eth_price.load()) +
                            ",\"sol_price\":" +
                            std::to_string(telemetry.sol_price.load()) +
                            "}";

        spine.write_json(event);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return 0;
}
