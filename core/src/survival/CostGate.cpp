#include "chimera/survival/CostGate.hpp"

namespace chimera {

CostGateDecision CostGate::evaluate(
    const std::string& symbol,
    bool is_maker,
    double expected_edge_bps,
    double qty,
    double latency_ms
) {
    CostGateDecision out;

    SurvivalDecision s =
        filter.evaluate(
            symbol,
            is_maker,
            expected_edge_bps,
            qty,
            latency_ms
        );

    out.edge_bps = expected_edge_bps;
    out.cost_bps = s.cost_bps;
    out.margin_bps = expected_edge_bps - s.cost_bps;

    if (!s.allowed) {
        out.pass = false;
        out.reason = s.block_reason;
        return out;
    }

    // HARD FLOOR — Phase A rule
    if (out.cost_bps < 6.5) {
        out.pass = false;
        out.reason = "COST_FLOOR_VIOLATION";
        return out;
    }

    if (out.margin_bps < 0.0) {
        out.pass = false;
        out.reason = "NEGATIVE_EXPECTANCY";
        return out;
    }

    out.pass = true;
    out.reason = "PASS";
    return out;
}

}
