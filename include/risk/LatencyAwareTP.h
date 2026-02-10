#pragma once

#include "execution/LatencyExecutionGovernor.hpp"
#include <string>

struct TPDecision {
    double tp_multiplier;
    const char* reason;
};

class LatencyAwareTP {
public:
    LatencyAwareTP();

    TPDecision compute(
        const std::string& symbol,
        LatencyRegime regime,
        double base_tp_points
    ) const;

private:
    TPDecision tp_xau(LatencyRegime regime) const;
    TPDecision tp_xag(LatencyRegime regime) const;

private:
    // === XAU ===
    static constexpr double XAU_FAST_MULT     = 1.35;
    static constexpr double XAU_NORMAL_MULT   = 1.00;
    static constexpr double XAU_DEGRADED_MULT = 0.65;

    // === XAG ===
    static constexpr double XAG_FAST_MULT     = 1.25;
    static constexpr double XAG_NORMAL_MULT   = 1.00;
    static constexpr double XAG_DEGRADED_MULT = 0.70;
};
