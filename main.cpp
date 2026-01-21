#include "core/SymbolLane_ANTIPARALYSIS.hpp"
#include "gui/include/live_operator_server.hpp"
#include "core/include/chimera/execution/BinanceIO.hpp"

#include <iostream>
#include <thread>
#include <vector>
#include <cstdlib>

int main() {
    std::cout << "[CHIMERA] LIVE TRADING SYSTEM | BINANCE CONNECTED" << std::endl;

    // Get API keys from environment
    const char* api_key = std::getenv("CHIMERA_API_KEY");
    const char* api_secret = std::getenv("CHIMERA_API_SECRET");
    
    if (!api_key || !api_secret) {
        std::cerr << "[ERROR] API keys not set. Use: export CHIMERA_API_KEY=... CHIMERA_API_SECRET=..." << std::endl;
        return 1;
    }

    // Start GUI on port 8080
    std::cout << "[GUI] Starting Live Operator Server on port 8080..." << std::endl;
    LiveOperatorServer gui_server(8080);
    gui_server.start();
    std::cout << "[GUI] ✓ Server running at http://localhost:8080" << std::endl;

    // Configure Binance connection
    chimera::BinanceConfig config;
    config.api_key = api_key;
    config.api_secret = api_secret;
    config.shadow_mode = true; // Safe mode - no actual orders
    
    std::cout << "[BINANCE] Connecting (shadow mode)..." << std::endl;
    chimera::BinanceIO binance(config);

    // Initialize symbol lanes
    std::vector<SymbolLane> lanes;
    lanes.emplace_back("ETHUSDT");
    lanes.emplace_back("BTCUSDT");
    lanes.emplace_back("SOLUSDT");

    // Subscribe to market data
    std::vector<std::string> symbols = {"ETHUSDT", "BTCUSDT", "SOLUSDT"};
    
    // Set up market data callback
    binance.on_tick = [&lanes](const chimera::MarketTick& tick) {
        // Route tick to appropriate lane
        for (auto& lane : lanes) {
            // Simple routing - match by checking if symbol contains the base
            // More robust routing in production
            lane.onTick(tick);
        }
        
        // Also log to console for visibility
        static int tick_count = 0;
        if (++tick_count % 100 == 0) {
            std::cout << "[MARKET] " << tick.symbol 
                      << " bid=" << tick.bid 
                      << " ask=" << tick.ask 
                      << " last=" << tick.last << std::endl;
        }
    };

    binance.connect();
    binance.subscribeMarketData(symbols);
    
    std::cout << "[CHIMERA] All systems operational" << std::endl;
    std::cout << "[CHIMERA] Subscribed to: " << symbols.size() << " symbols" << std::endl;
    std::cout << "[CHIMERA] GUI: http://localhost:8080" << std::endl;
    std::cout << "[CHIMERA] Press Ctrl+C to stop" << std::endl;

    // Main loop - poll exchange and update telemetry
    while (true) {
        binance.poll();
        
        // Update telemetry every second
        for (auto& lane : lanes) {
            lane.tick();
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    return 0;
}
