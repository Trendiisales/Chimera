// =============================================================================
// cfd_test.cpp - Chimera v6 CFD Engine FIX Connection Test
// =============================================================================
// Tests TRADE-first FIX connection to cTrader
// Configuration loaded from config.ini - NO HARDCODED CREDENTIALS
// =============================================================================
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

#include "fix/CTraderFIXClient.hpp"
#include "fix/FIXConfig.hpp"

std::atomic<bool> g_running{true};

void signalHandler(int sig) {
    std::cout << "\nReceived signal " << sig << ", shutting down...\n";
    g_running = false;
}

int main() {
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  CHIMERA v6.15 - cTrader FIX Connection Test\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n";
    std::cout << "  Configuration: config.ini\n";
    std::cout << "  Session Order: TRADE first, then QUOTE\n";
    std::cout << "═══════════════════════════════════════════════════════════════\n\n";
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Load and validate configuration
    Chimera::FIXConfig config;
    config.print();
    
    if (!config.isValid()) {
        std::cerr << "\n[ERROR] Invalid configuration - check config.ini\n";
        return 1;
    }
    
    // Create FIX client
    Chimera::CTraderFIXClient client;
    client.setConfig(config);
    
    // Track ticks
    std::atomic<int> tickCount{0};
    client.setOnTick([&tickCount](const Chimera::CTraderTick& tick) {
        int count = tickCount.fetch_add(1);
        if (count < 5 || count % 100 == 0) {
            std::cout << "[TICK #" << count << "] " << tick.symbol 
                      << " bid=" << tick.bid << " ask=" << tick.ask 
                      << " spread=" << (tick.spread() * 10000) << "pips\n";
        }
    });
    
    // Track state changes
    client.setOnState([](bool quoteUp, bool tradeUp) {
        std::cout << "[STATE] TRADE=" << (tradeUp ? "UP" : "DOWN")
                  << " QUOTE=" << (quoteUp ? "UP" : "DOWN") << "\n";
    });
    
    // Connect (TRADE first, then QUOTE)
    std::cout << "\n[TEST] Connecting to cTrader FIX...\n";
    
    bool connected = client.connect();
    
    if (!connected) {
        std::cout << "\n[RESULT] Connection FAILED\n";
        std::cout << "[RESULT] State: " << Chimera::toString(client.getState()) << "\n";
        std::cout << "\n[DEBUG] Check config.ini settings\n";
        return 1;
    }
    
    std::cout << "\n[RESULT] Connection SUCCESS!\n";
    std::cout << "[RESULT] State: " << Chimera::toString(client.getState()) << "\n";
    
    // Subscribe to some symbols
    std::cout << "\n[TEST] Subscribing to market data...\n";
    client.subscribeMarketData("EURUSD");
    client.subscribeMarketData("XAUUSD");
    
    // Run for 30 seconds collecting ticks
    std::cout << "[TEST] Collecting ticks for 30 seconds...\n\n";
    
    auto start = std::chrono::steady_clock::now();
    while (g_running) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        
        if (elapsed >= 30) break;
        
        if (elapsed > 0 && elapsed % 10 == 0) {
            static int lastReport = 0;
            if (elapsed != lastReport) {
                lastReport = elapsed;
                std::cout << "[STATUS] " << elapsed << "s: " 
                          << tickCount.load() << " ticks received\n";
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Print stats
    client.printStats();
    
    // Disconnect
    std::cout << "\n[TEST] Disconnecting...\n";
    client.disconnect();
    
    std::cout << "\n[RESULT] Test complete. Total ticks: " << tickCount.load() << "\n";
    
    return 0;
}
