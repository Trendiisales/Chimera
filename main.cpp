#include "chimera/execution/BinanceIO.hpp"
#include "chimera/execution/Hash.hpp"
#include "chimera/telemetry/TelemetryServer.hpp"
#include "chimera/telemetry_bridge/GuiState.hpp"
#include "chimera/SymbolLane.hpp"

#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdlib>
#include <stdexcept>
#include <chrono>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{true};

void handle_signal(int) {
    std::cout << "\n[CHIMERA] Shutdown signal received..." << std::endl;
    g_running = false;
}

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    
    std::cout << "=========================================" << std::endl;
    std::cout << "[CHIMERA] v3.6 INSTITUTIONAL HFT SYSTEM" << std::endl;
    std::cout << "=========================================" << std::endl;

    // Start telemetry server (real HTTP server on port 8080)
    std::cout << "[GUI] Starting Dashboard on port 8080..." << std::endl;
    chimera::TelemetryServer telemetry_server(8080);
    telemetry_server.start();
    std::cout << "[GUI] ✓ Dashboard: http://localhost:8080" << std::endl;

    // Get API keys from environment
    const char* api_key = std::getenv("CHIMERA_API_KEY");
    const char* api_secret = std::getenv("CHIMERA_API_SECRET");
    
    if (!api_key || !api_secret) {
        std::cerr << "[ERROR] API keys not set." << std::endl;
        std::cerr << "Usage: export CHIMERA_API_KEY=... CHIMERA_API_SECRET=..." << std::endl;
        std::cerr << "[INFO] Running in demo mode (no trading)" << std::endl;
        
        std::cout << "\n[DEMO] System initialized successfully" << std::endl;
        std::cout << "[DEMO] Dashboard: http://localhost:8080" << std::endl;
        std::cout << "[DEMO] Set API keys to enable live trading" << std::endl;
        std::cout << "[DEMO] Press Ctrl+C to exit" << std::endl;
        
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        std::cout << "[CHIMERA] Shutdown complete" << std::endl;
        return 0;
    }

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

    // Initialize GuiState symbols so SymbolLane can update them
    std::cout << "[GUI] Initializing telemetry symbols..." << std::endl;
    {
        auto& gui = chimera::GuiState::instance();
        std::lock_guard<std::mutex> lock(gui.mtx);
        
        for (const auto& [sym, hash] : symbol_hashes) {
            chimera::SymbolState ss;
            ss.symbol = sym;
            ss.hash = hash;
            ss.engine = "CRYPTO";
            ss.enabled = true;
            gui.symbols.push_back(ss);
            std::cout << "[GUI] Registered " << sym << " (0x" 
                      << std::hex << hash << std::dec << ")" << std::endl;
        }
    }

    // Create Lane objects for each symbol
    std::cout << "[LANES] Creating symbol lanes..." << std::endl;
    std::vector<std::unique_ptr<Lane>> lanes;
    std::unordered_map<uint32_t, Lane*> lane_router;
    
    for (const auto& [sym, hash] : symbol_hashes) {
        lanes.push_back(std::make_unique<Lane>(sym, hash));
        lane_router[hash] = lanes.back().get();
    }

    // Set up market data callback - route to lanes
    binance.on_tick = [&lane_router](const chimera::MarketTick& tick) {
        auto it = lane_router.find(tick.symbol_hash);
        if (it != lane_router.end()) {
            it->second->on_tick(tick);
        }
    };

    binance.connect();
    binance.subscribeMarketData(symbols);
    
    std::cout << "=========================================" << std::endl;
    std::cout << "[CHIMERA] All systems operational" << std::endl;
    std::cout << "[CHIMERA] Subscribed: " << symbols.size() << " symbols" << std::endl;
    std::cout << "[CHIMERA] Dashboard: http://localhost:8080" << std::endl;
    std::cout << "[CHIMERA] Press Ctrl+C to stop" << std::endl;
    std::cout << "=========================================" << std::endl;

    // Main loop
    while (g_running) {
        binance.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[CHIMERA] Shutting down gracefully..." << std::endl;
    binance.disconnect();
    telemetry_server.stop();
    std::cout << "[CHIMERA] Shutdown complete" << std::endl;
    return 0;
}
