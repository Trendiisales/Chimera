#pragma once
// ═══════════════════════════════════════════════════════════════════════════
// ExecutionExposure - Tracks temporary exposure during sliced execution
// v4.2.2: For micro-hedging partial fills
// ═══════════════════════════════════════════════════════════════════════════

#include <cmath>
#include <cstdint>

namespace Chimera {

struct ExecutionExposure {
    double filled_qty = 0.0;    // Actually filled so far
    double target_qty = 0.0;    // Target total quantity
    double hedge_qty = 0.0;     // Active hedge quantity
    
    // Net unhedged exposure
    double net_exposure() const {
        return filled_qty - target_qty + hedge_qty;
    }
    
    // Do we have unhedged risk?
    bool has_risk() const {
        return std::abs(net_exposure()) > 0.0001;
    }
    
    // Filled percentage
    double fill_pct() const {
        return (target_qty > 0) ? (filled_qty / target_qty) : 0.0;
    }
    
    // Is execution complete?
    bool complete() const {
        return std::abs(filled_qty - target_qty) < 0.0001;
    }
    
    // Record a fill
    void record_fill(double qty) {
        filled_qty += qty;
    }
    
    // Record hedge
    void record_hedge(double qty) {
        hedge_qty += qty;
    }
    
    // Clear hedge (after unwind)
    void clear_hedge() {
        hedge_qty = 0.0;
    }
    
    // Reset for new execution
    void reset(double target) {
        filled_qty = 0.0;
        target_qty = target;
        hedge_qty = 0.0;
    }
};

}  // namespace Chimera
