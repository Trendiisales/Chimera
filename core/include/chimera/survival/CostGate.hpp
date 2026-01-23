#pragma once
#include <string>
#include "chimera/survival/EdgeSurvivalFilter.hpp"

namespace chimera {

struct CostGateDecision {
    bool pass = false;
    double edge_bps = 0.0;
    double cost_bps = 0.0;
    double margin_bps = 0.0;
    std::string reason;
};

class CostGate {
public:
    explicit CostGate(EdgeSurvivalFilter& esf)
        : filter(esf) {}

    CostGateDecision evaluate(
        const std::string& symbol,
        bool is_maker,
        double expected_edge_bps,
        double qty,
        double latency_ms
    );

private:
    EdgeSurvivalFilter& filter;
};

}
