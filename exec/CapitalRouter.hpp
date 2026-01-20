#pragma once

#include <string>
#include <atomic>

struct DecisionResult;

class CapitalRouter {
public:
    CapitalRouter() = default;

    // MAIN ROUTE — position sizing + capital clamp
    inline DecisionResult route(
        const std::string& engine,
        bool is_buy,
        double confidence,
        double px,
        double capital_usd
    );

    inline void setMaxRiskBps(double bps) {
        max_risk_bps_.store(bps, std::memory_order_relaxed);
    }

private:
    std::atomic<double> max_risk_bps_{50.0}; // default 0.5%
};

