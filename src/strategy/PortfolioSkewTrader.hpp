#pragma once
#include <string>
#include <cstdint>
#include <map>
#include "core/contract.hpp"

namespace chimera {

// PortfolioSkewTrader - Balances portfolio across all symbols
// Monitors aggregate position and hedges when portfolio becomes skewed.
// Works across BTCUSDT, ETHUSDT, SOLUSDT for portfolio-level risk management.
// 8bps edge, 0.3x size multiplier.
class PortfolioSkewTrader : public IEngine {
public:
    PortfolioSkewTrader();
    const std::string& id() const override;
    void onTick(const MarketTick& tick, std::vector<OrderIntent>& out) override;

private:
    std::string engine_id_;
    
    // Per-symbol throttling
    struct SymbolState {
        uint64_t last_submit_ns = 0;
    };
    std::map<std::string, SymbolState> state_;
    
    // Track portfolio-level positions (updated each tick)
    std::map<std::string, double> portfolio_pos_;
    
    static constexpr double MAX_POS_PER_SYMBOL = 0.05;
    static constexpr double BASE_QTY = 0.008;  // Smaller size for rebalancing
    static constexpr double EDGE_BPS = 8.0;
    static constexpr double SKEW_THRESHOLD = 0.03;  // Trigger when any symbol > 3% skewed
    static constexpr uint64_t THROTTLE_NS = 100'000'000ULL;  // 100ms per symbol
    static constexpr double PORTFOLIO_K = 0.5;  // Portfolio skew sensitivity
};

}
