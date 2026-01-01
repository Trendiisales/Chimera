#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// MicroHedgeExecutor - Temporary hedging during sliced execution
// v4.2.2: Neutralizes unintended exposure from partial fills
// ═══════════════════════════════════════════════════════════════════════════

#include "ExecutionExposure.hpp"
#include <cmath>
#include <iostream>

namespace Chimera {

// Forward declaration - actual implementation depends on exchange connector
class OrderRouter;

class MicroHedgeExecutor {
public:
    // Minimum exposure to trigger hedge (avoid micro-dust)
    static constexpr double MIN_HEDGE_QTY = 0.0001;
    
    // Maximum hedge as fraction of target (safety)
    static constexpr double MAX_HEDGE_RATIO = 0.5;
    
    // Check if hedging is needed
    bool needs_hedge(const ExecutionExposure& ex) const {
        double exposure = std::abs(ex.net_exposure());
        return exposure > MIN_HEDGE_QTY;
    }
    
    // Calculate hedge quantity
    double calc_hedge_qty(const ExecutionExposure& ex) const {
        double exposure = ex.net_exposure();
        
        // Cap hedge at MAX_HEDGE_RATIO of target
        double max_hedge = ex.target_qty * MAX_HEDGE_RATIO;
        double abs_exposure = std::abs(exposure);
        
        if (abs_exposure > max_hedge) {
            // Something is very wrong - log and cap
            std::cerr << "[HEDGE] WARNING: Exposure " << abs_exposure 
                      << " exceeds max hedge " << max_hedge << "\n";
            return (exposure > 0) ? max_hedge : -max_hedge;
        }
        
        return -exposure;  // Negative of exposure to neutralize
    }
    
    // Determine hedge side
    bool hedge_is_buy(double exposure) const {
        return exposure < 0;  // Negative exposure = need to buy to hedge
    }
    
    // Log hedge action (actual sending is done by OrderRouter)
    void log_hedge(const char* symbol, double qty, bool is_buy) const {
        std::cout << "[HEDGE " << symbol << "] " 
                  << (is_buy ? "BUY" : "SELL") << " " << std::abs(qty) << "\n";
    }
    
    // Check if hedge should be unwound (execution complete)
    bool should_unwind(const ExecutionExposure& ex) const {
        return ex.complete() && std::abs(ex.hedge_qty) > MIN_HEDGE_QTY;
    }
    
    // Calculate unwind quantity
    double calc_unwind_qty(const ExecutionExposure& ex) const {
        return -ex.hedge_qty;  // Opposite of hedge to close
    }
};

}  // namespace Chimera
