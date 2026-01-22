#include "core/SymbolLane_ANTIPARALYSIS.hpp"
#include "gui/include/live_operator_server.hpp"
#include "core/include/chimera/execution/BinanceIO.hpp"
#include "core/include/chimera/execution/Hash.hpp"

#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <cstdlib>
#include <stdexcept>

int main() {
    std::cout << "=========================================" << std::endl;
    std::cout << "[CHIMERA] LIVE TRADING SYSTEM | BINANCE" << std::endl;
    std::cout << "=========================================" << std::endl;

    // Get API keys from environment
    const char* api_key = std::getenv("CHIMERA_API_KEY");
    const char* api_secret = std::getenv("CHIMERA_API_SECRET");
    
    if (!api_key || !api_secret) {
        std::cerr << "[ERROR] API keys not set." << std::endl;
        std::cerr << "Usage: export CHIMERA_API_KEY=... CHIMERA_API_SECRET=..." << std::endl;
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
    config.shadow_mode = true;
    
    std::cout << "[BINANCE] Connecting (shadow mode)..." << std::endl;
    chimera::BinanceIO binance(config);

    // Define symbols
    std::vector<std::string> symbols = {"ETHUSDT", "BTCUSDT", "SOLUSDT"};
    
    // Pre-compute hashes and validate for collisions
    std::unordered_map<uint32_t, std::string> hash_registry;
    std::vector<std::pair<std::string, uint32_t>> symbol_hashes;
    
    std::cout << "[ROUTING] Computing symbol hashes..." << std::endl;
    for (const auto& sym : symbols) {
        uint32_t h = chimera::fnv1a_32(sym);
        
        // Collision detection
        if (hash_registry.count(h)) {
            throw std::runtime_error(
                "SYMBOL HASH COLLISION: " + 
                hash_registry[h] + " vs " + sym +
                " (hash=0x" + std::to_string(h) + ")"
            );
        }
        
        hash_registry[h] = sym;
        symbol_hashes.emplace_back(sym, h);
        
        std::cout << "[ROUTING] " << sym << " -> 0x" 
                  << std::hex << h << std::dec << std::endl;
    }

    // Initialize symbol lanes with pre-computed hashes
    std::vector<SymbolLane> lanes;
    std::unordered_map<uint32_t, SymbolLane*> lane_by_symbol;
    
    std::cout << "[SUPERVISOR] Initializing lanes..." << std::endl;
    for (const auto& [sym, hash] : symbol_hashes) {
        lanes.emplace_back(sym, hash);
    }
    
    // Register lanes in routing map
    for (auto& lane : lanes) {
        lane_by_symbol[lane.symbolHash()] = &lane;
    }
    
    std::cout << "[ROUTING] Lane map built: " << lane_by_symbol.size() 
              << " lanes registered" << std::endl;

    // Set up market data callback with O(1) routing
    binance.on_tick = [&lane_by_symbol](const chimera::MarketTick& tick) {
        // O(1) hash lookup - no loops, no string comparisons
        auto it = lane_by_symbol.find(tick.symbol_hash);
        if (it != lane_by_symbol.end()) {
            it->second->onTick(tick);
        } else {
            // Unroutable tick - log warning
            static int unroutable_count = 0;
            if (++unroutable_count % 100 == 1) {
                std::cerr << "[ROUTING] Unroutable tick: " << tick.symbol
                          << " hash=0x" << std::hex << tick.symbol_hash 
                          << std::dec << std::endl;
            }
        }
        
        // Log to console for visibility
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
    
    std::cout << "=========================================" << std::endl;
    std::cout << "[CHIMERA] All systems operational" << std::endl;
    std::cout << "[CHIMERA] Subscribed: " << symbols.size() << " symbols" << std::endl;
    std::cout << "[CHIMERA] GUI: http://localhost:8080" << std::endl;
    std::cout << "[CHIMERA] Metrics: http://localhost:9100/metrics" << std::endl;
    std::cout << "[CHIMERA] Press Ctrl+C to stop" << std::endl;
    std::cout << "=========================================" << std::endl;

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
