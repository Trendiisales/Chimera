#pragma once
#include "tier1/AtomicPositionGate.hpp"
#include <string>

namespace chimera {

// ---------------------------------------------------------------------------
// CapView: Read-only interface for strategies to check position state
// 
// Problem solved:
//   OLD: Strategy generates signal → router checks cap → BLOCKED → spam
//   NEW: Strategy checks cap first → only generates if possible → clean
// 
// This stops 90% of the POSITION_GATE BLOCK spam.
// 
// Usage (in strategy):
//   if (!cap_view.can_trade(qty))
//       return;  // Don't even generate signal
//   
//   // Now send signal...
// ---------------------------------------------------------------------------

class CapView {
public:
    explicit CapView(const AtomicPositionGate& gate, const std::string& symbol)
        : gate_(gate), symbol_(symbol) {}

    // Current position for this strategy's symbol
    double position() const {
        return gate_.get_position(symbol_);
    }

    // Position cap for this strategy's symbol
    double cap() const {
        return gate_.get_cap(symbol_);
    }

    // Remaining capacity before hitting cap
    double remaining() const {
        return cap() - std::abs(position());
    }

    // Check if we can trade this qty without violating cap
    // Call this BEFORE generating signals
    bool can_trade(double qty) const {
        return gate_.allow(symbol_, qty);
    }

    // Utilization ratio (0.0 = empty, 1.0 = at cap)
    double utilization() const {
        double c = cap();
        if (c <= 0.0)
            return 0.0;
        return std::abs(position()) / c;
    }

private:
    const AtomicPositionGate& gate_;
    std::string symbol_;
};

} // namespace chimera
