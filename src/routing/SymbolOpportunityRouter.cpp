#include "routing/SymbolOpportunityRouter.hpp"

SymbolOpportunityRouter::SymbolOpportunityRouter() {}

OpportunityDecision SymbolOpportunityRouter::allow_trade(
    const std::string& symbol,
    double p95_ms,
    double velocity,
    LatencyRegime regime,
    int hour_utc
) const {
    if (symbol == "XAUUSD") {
        return route_xau(p95_ms, velocity, regime, hour_utc);
    }
    if (symbol == "XAGUSD") {
        return route_xag(p95_ms, velocity, regime, hour_utc);
    }

    return { false, "SYMBOL_NOT_ROUTED" };
}

// =======================
// XAU ROUTING (STRICT)
// =======================
OpportunityDecision SymbolOpportunityRouter::route_xau(
    double p95_ms,
    double velocity,
    LatencyRegime regime,
    int hour_utc
) const {
    if (regime != LatencyRegime::FAST) {
        return { false, "XAU_NOT_FAST" };
    }

    if (p95_ms > XAU_MAX_P95_FAST) {
        return { false, "XAU_LATENCY_TOO_HIGH" };
    }

    const bool london_open =
        hour_utc >= LONDON_OPEN_START &&
        hour_utc <= LONDON_OPEN_END;

    const bool ny_open =
        hour_utc >= NY_OPEN_START &&
        hour_utc <= NY_OPEN_END;

    const double impulse_threshold =
        (london_open || ny_open)
            ? XAU_MIN_IMPULSE_OPEN
            : XAU_MIN_IMPULSE_FAST;

    double abs_velocity = (velocity < 0) ? -velocity : velocity;
    if (abs_velocity < impulse_threshold) {
        return { false, "XAU_NO_IMPULSE" };
    }

    return { true, "XAU_OK" };
}

// =======================
// XAG ROUTING (FLEXIBLE)
// =======================
OpportunityDecision SymbolOpportunityRouter::route_xag(
    double p95_ms,
    double velocity,
    LatencyRegime regime,
    int hour_utc
) const {
    if (regime == LatencyRegime::HALT) {
        return { false, "XAG_HALTED" };
    }

    if (p95_ms > XAG_MAX_P95_FAST) {
        return { false, "XAG_LATENCY_TOO_HIGH" };
    }

    double abs_velocity = (velocity < 0) ? -velocity : velocity;
    if (abs_velocity < XAG_MIN_IMPULSE_FAST) {
        return { false, "XAG_NO_IMPULSE" };
    }

    return { true, "XAG_OK" };
}
