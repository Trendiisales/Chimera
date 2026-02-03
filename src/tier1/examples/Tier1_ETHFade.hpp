#pragma once
#include "tier1/Tier1BaseStrategy.hpp"
#include <thread>
#include <chrono>
#include <iostream>

namespace chimera {

// ---------------------------------------------------------------------------
// Tier1_ETHFade: Example strategy using lock-free submission
// 
// This demonstrates:
//   1. Checking cap before signal generation (prevents spam)
//   2. Lock-free signal submission
//   3. Backpressure handling
// ---------------------------------------------------------------------------

class Tier1_ETHFade : public Tier1BaseStrategy {
public:
    Tier1_ETHFade(Tier1ExecutionRouter& router,
                  AtomicPositionGate& gate)
        : Tier1BaseStrategy(router, gate, "ETHUSDT", "ETH_FADE") {}

    void on_tick() override {
        double qty = -0.01;  // Fade = short
        double price = 2214.89;
        double edge = 3.2;
        
        // Check cap BEFORE generating signal
        if (!can_trade(qty)) {
            // At cap - don't spam
            return;
        }
        
        // Send signal (lock-free)
        if (!send(qty, price, edge)) {
            // Ring full - backpressure
            // This is normal under heavy load
            std::cout << "[ETH_FADE] Backpressure - ring full" << std::endl;
        }
    }

    // Run loop (call this in a thread)
    void run() {
        while (true) {
            on_tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
};

} // namespace chimera
