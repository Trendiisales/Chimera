#pragma once
#include "chimera/causal_lab/EventTypes.hpp"
#include <functional>
#include <vector>
#include <string>

namespace chimera_lab {

struct ShadowResult {
    uint64_t trade_id;
    std::string variant;
    bool would_trade;
    double expected_pnl;
};

class ShadowStrategy {
public:
    virtual ~ShadowStrategy() = default;
    virtual std::string name() const = 0;
    virtual bool decide(const SignalVector& s, double price, double& qty) = 0;
    virtual double simulateFill(double price, double qty) = 0;
};

class ShadowFarm {
public:
    void add(ShadowStrategy* strat);

    std::vector<ShadowResult> evaluate(uint64_t trade_id,
                                       const SignalVector& s,
                                       double price);

    // Real PnL calculation (use this instead of fake price*qty)
    static double calculateRealPnL(
        double entry_price,
        double exit_price,
        double qty,
        int side,          // +1 for long, -1 for short
        double fee_bps,    // e.g., 10 for 10 bps
        double slippage_bps // e.g., 5 for 5 bps
    );

private:
    std::vector<ShadowStrategy*> strategies;
};

} // namespace chimera_lab
