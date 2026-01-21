#include "ShadowFarm.hpp"
#include <cmath>

namespace chimera_lab {

void ShadowFarm::add(ShadowStrategy* strat) {
    strategies.push_back(strat);
}

std::vector<ShadowResult> ShadowFarm::evaluate(uint64_t trade_id,
                                               const SignalVector& s,
                                               double price) {
    std::vector<ShadowResult> results;

    for (auto* strat : strategies) {
        double qty = 0.0;
        bool trade = strat->decide(s, price, qty);
        double pnl = 0.0;

        if (trade) {
            pnl = strat->simulateFill(price, qty);
        }

        results.push_back({
            trade_id,
            strat->name(),
            trade,
            pnl
        });
    }

    return results;
}

// Real PnL calculation helper (for strategies to use)
double ShadowFarm::calculateRealPnL(
    double entry_price,
    double exit_price,
    double qty,
    int side,  // +1 for long, -1 for short
    double fee_bps,
    double slippage_bps) {
    
    // Gross P&L
    double gross = (exit_price - entry_price) * side * qty;
    
    // Fees (both entry and exit)
    double fees = qty * entry_price * (fee_bps / 10000.0) * 2;
    
    // Slippage
    double slippage = qty * entry_price * (slippage_bps / 10000.0);
    
    // Net P&L
    return gross - fees - slippage;
}

} // namespace chimera_lab
