#include "core/SymbolLane_ANTIPARALYSIS.hpp"
#include "telemetry/TelemetryServer.hpp"
#include "execution/ShadowExecutor.hpp"
#include "gui/include/live_operator_server.hpp"

#include <iostream>
#include <thread>

int main() {
    std::cout << "[CHIMERA] MODE B LIVE STACK | SHADOW EXEC | TELEMETRY + GUI" << std::endl;

    // Start GUI operator server on port 8080
    std::cout << "[GUI] Starting Live Operator Server on port 8080..." << std::endl;
    LiveOperatorServer gui_server(8080);
    gui_server.start();
    std::cout << "[GUI] ✓ Server running at http://localhost:8080" << std::endl;

    // Initialize trading engines
    SymbolLane eth("ETH_PERP");
    SymbolLane btc("BTC_PERP");
    SymbolLane sol("SOL_SPOT");

    std::cout << "[CHIMERA] All systems operational" << std::endl;
    std::cout << "[CHIMERA] GUI: http://localhost:8080" << std::endl;
    std::cout << "[CHIMERA] Press Ctrl+C to stop" << std::endl;

    // Keep main thread alive
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
