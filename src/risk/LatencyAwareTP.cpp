#include "risk/LatencyAwareTP.h"

LatencyAwareTP::LatencyAwareTP() {}

TPDecision LatencyAwareTP::compute(
    const std::string& symbol,
    LatencyRegime regime,
    double base_tp_points
) const {
    if (symbol == "XAUUSD") {
        return tp_xau(regime);
    }
    if (symbol == "XAGUSD") {
        return tp_xag(regime);
    }

    return { 1.0, "NO_SYMBOL_OVERRIDE" };
}

// =======================
// XAU
// =======================
TPDecision LatencyAwareTP::tp_xau(LatencyRegime regime) const {
    switch (regime) {
        case LatencyRegime::FAST:
            return { XAU_FAST_MULT, "XAU_FAST_LATENCY" };
        case LatencyRegime::NORMAL:
            return { XAU_NORMAL_MULT, "XAU_NORMAL_LATENCY" };
        case LatencyRegime::DEGRADED:
            return { XAU_DEGRADED_MULT, "XAU_DEGRADED_LATENCY" };
        case LatencyRegime::HALT:
            return { XAU_DEGRADED_MULT, "XAU_HALT_LATENCY" };
    }
    return { 1.0, "XAU_DEFAULT" };
}

// =======================
// XAG
// =======================
TPDecision LatencyAwareTP::tp_xag(LatencyRegime regime) const {
    switch (regime) {
        case LatencyRegime::FAST:
            return { XAG_FAST_MULT, "XAG_FAST_LATENCY" };
        case LatencyRegime::NORMAL:
            return { XAG_NORMAL_MULT, "XAG_NORMAL_LATENCY" };
        case LatencyRegime::DEGRADED:
            return { XAG_DEGRADED_MULT, "XAG_DEGRADED_LATENCY" };
        case LatencyRegime::HALT:
            return { XAG_DEGRADED_MULT, "XAG_HALT_LATENCY" };
    }
    return { 1.0, "XAG_DEFAULT" };
}
