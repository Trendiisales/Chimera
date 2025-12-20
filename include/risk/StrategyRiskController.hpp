#pragma once
#include "strategy/StrategyTypes.hpp"
#include "risk/RiskTypes.hpp"

namespace Chimera {

class StrategyRiskController {
public:
    StrategyRiskController();

    RiskDecision assess(
        const StrategyDecision& decision,
        double volatility,
        bool kill_switch_active,
        uint64_t ts_ns
    );

private:
    double max_confidence_;
    double vol_limit_;
    uint64_t min_interval_ns_;
    uint64_t last_action_ts_;
};

}
