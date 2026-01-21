#pragma once

#include <string>
#include <unordered_map>

#include "chimera/execution/MarketBus.hpp"
#include "chimera/execution/PositionBook.hpp"

namespace chimera {

struct SurvivalDecision {
    bool allowed = false;
    double expected_bps = 0.0;
    double cost_bps = 0.0;
    std::string block_reason;
};

struct FeeModel {
    double maker_bps = 0.2;
    double taker_bps = 1.0;
};

class EdgeSurvivalFilter {
public:
    explicit EdgeSurvivalFilter(MarketBus& market);

    SurvivalDecision evaluate(
        const std::string& symbol,
        bool is_maker,
        double expected_edge_bps,
        double qty,
        double latency_ms
    );

    void setMinSurvivalBps(double bps);
    void setFeeModel(const FeeModel& f);

private:
    double estimateSlippageBps(
        const std::string& symbol,
        double qty
    ) const;

    double estimateLatencyBps(
        const std::string& symbol,
        double latency_ms
    ) const;

    double estimateFundingBps(
        const std::string& symbol
    ) const;

private:
    MarketBus& market_bus;

    double min_survival_bps = 6.5;
    FeeModel fees;
};

}
