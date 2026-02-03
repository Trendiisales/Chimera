#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>

#include "tier1/Tier1ExecutionRouter.hpp"
#include "tier1/AtomicPositionGate.hpp"
#include "tier1/examples/Tier1_ETHFade.hpp"
#include "runtime/Context.hpp"

using namespace chimera;

// ---------------------------------------------------------------------------
// Tier1 Demonstration
// 
// This shows the new lock-free architecture in action:
//   - Single-writer router on pinned core
//   - Lock-free signal ring
//   - Atomic position gate
//   - No mutex contention
// 
// Expected results:
//   - Clean logs (no BLOCK spam)
//   - Fast execution (5-15µs decision latency)
//   - Strategies don't block each other
// ---------------------------------------------------------------------------

static std::atomic<bool> g_shutdown{false};

void handle_sigint(int) {
    g_shutdown.store(true, std::memory_order_release);
}

int main() {
    std::cout << "=== TIER 1 LOCK-FREE ARCHITECTURE ===" << std::endl;
    std::cout << "Architecture:" << std::endl;
    std::cout << "  Strategies (N threads)" << std::endl;
    std::cout << "       ↓ (lock-free push)" << std::endl;
    std::cout << "  SignalRing<4096>" << std::endl;
    std::cout << "       ↓ (single thread, pinned core)" << std::endl;
    std::cout << "  ExecutionRouter" << std::endl;
    std::cout << "       ↓ (atomic updates)" << std::endl;
    std::cout << "  PositionGate" << std::endl;
    std::cout << "       ↓" << std::endl;
    std::cout << "  Exchange / Journal" << std::endl;
    std::cout << std::endl;

    // Setup signal handler
    signal(SIGINT, handle_sigint);

    // Create context
    Context ctx;

    // Create atomic position gate
    AtomicPositionGate gate;
    gate.set_cap("ETHUSDT", 0.05);
    gate.set_cap("BTCUSDT", 0.05);
    gate.set_cap("SOLUSDT", 0.05);

    // Create Tier1 router
    Tier1ExecutionRouter router(ctx, gate);
    router.start();

    // Create strategies
    Tier1_ETHFade eth_fade(router, gate);

    // Start strategy threads
    std::thread eth_thread([&]() {
        std::cout << "[ETH_FADE] Strategy thread started" << std::endl;
        while (!g_shutdown.load(std::memory_order_relaxed)) {
            eth_fade.on_tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        std::cout << "[ETH_FADE] Strategy thread stopped" << std::endl;
    });

    // Main loop - monitor and report
    std::cout << std::endl;
    std::cout << "System running. Press Ctrl+C to stop." << std::endl;
    std::cout << std::endl;

    uint64_t report_counter = 0;
    while (!g_shutdown.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // Report positions
        if (++report_counter % 2 == 0) {
            std::cout << "[POSITIONS]"
                      << " ETH=" << gate.get_position("ETHUSDT")
                      << " BTC=" << gate.get_position("BTCUSDT")
                      << " SOL=" << gate.get_position("SOLUSDT")
                      << std::endl;
        }
    }

    // Shutdown
    std::cout << std::endl;
    std::cout << "Shutting down..." << std::endl;
    
    // Stop router
    router.stop();
    
    // Join strategy threads
    eth_thread.join();

    std::cout << "Shutdown complete." << std::endl;
    return 0;
}
