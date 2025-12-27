// =============================================================================
// crypto_test.cpp - Chimera v6 Crypto Engine Test
// =============================================================================
// Tests the Binance crypto engine compilation
// =============================================================================
#include <iostream>
#include <csignal>
#include <atomic>

#include "binance/BinanceEngine.hpp"

std::atomic<bool> g_running{true};

void signalHandler(int sig) {
    std::cout << "\nReceived signal " << sig << ", shutting down...\n";
    g_running = false;
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  CHIMERA v6 - Crypto Engine Test (Binance)\n";
    std::cout << "=========================================================\n";
    std::cout << "  This tests compilation of the Binance engine\n";
    std::cout << "=========================================================\n\n";
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Create shared state
    Chimera::GlobalKill global_kill;
    Chimera::DailyLossGuard daily_loss(-500.0);  // -$500 limit
    
    // Create engine
    Chimera::Binance::BinanceEngine engine(global_kill, daily_loss);
    
    std::cout << "[TEST] BinanceEngine created\n";
    std::cout << "[TEST] State: " << static_cast<int>(engine.state()) << "\n";
    
    // We won't actually connect in this test (DNS blocked in sandbox)
    std::cout << "[TEST] Compilation successful!\n";
    
    return 0;
}
