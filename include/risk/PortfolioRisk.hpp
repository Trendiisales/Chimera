#pragma once
#include "risk/RiskTypes.hpp"

namespace Chimera {

class PortfolioRisk {
public:
    PortfolioRisk();

    RiskDecision combine(
        const RiskDecision& strategy_risk,
        double arbiter_mult,
        bool venue_ok,
        uint64_t ts_ns
    );

private:
    double max_total_mult_;
};

}
