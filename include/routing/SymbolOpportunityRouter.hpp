#pragma once

#include "execution/LatencyExecutionGovernor.hpp"
#include <string>
#include <cstdint>

struct OpportunityDecision {
    bool allow;
    const char* reason;
};

class SymbolOpportunityRouter {
public:
    SymbolOpportunityRouter();

    OpportunityDecision allow_trade(
        const std::string& symbol,
        double p95_ms,
        double velocity,
        LatencyRegime regime,
        int hour_utc
    ) const;

private:
    OpportunityDecision route_xau(
        double p95_ms,
        double velocity,
        LatencyRegime regime,
        int hour_utc
    ) const;

    OpportunityDecision route_xag(
        double p95_ms,
        double velocity,
        LatencyRegime regime,
        int hour_utc
    ) const;

private:
    // === XAU CONFIG ===
    static constexpr double XAU_MIN_IMPULSE_FAST = 0.18;
    static constexpr double XAU_MIN_IMPULSE_OPEN = 0.14;
    static constexpr double XAU_MAX_P95_FAST     = 5.0;

    // === XAG CONFIG ===
    static constexpr double XAG_MIN_IMPULSE_FAST = 0.08;
    static constexpr double XAG_MAX_P95_FAST     = 6.5;

    // Session windows (UTC)
    static constexpr int LONDON_OPEN_START = 7;
    static constexpr int LONDON_OPEN_END   = 10;

    static constexpr int NY_OPEN_START = 12;
    static constexpr int NY_OPEN_END   = 16;
};
