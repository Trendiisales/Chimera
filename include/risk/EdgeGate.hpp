#pragma once
#include "risk/CostModel.hpp"
#include <string>
#include <algorithm>

class EdgeGate {
public:
    static bool allowEntry(
        const std::string& symbol,
        double expected_move,
        double bid,
        double ask,
        double lot_size,
        double latency_ms,
        double confidence
    ) {
        CostBreakdown cost = CostModel::compute(symbol, bid, ask, lot_size, latency_ms);
        
        double min_edge_multiple = (symbol == "XAUUSD") ? 1.4 : 2.0;
        double confidence_boost = std::min(1.3, 1.0 + (confidence - 0.5));
        double required_edge = cost.total_cost * min_edge_multiple * confidence_boost;
        
        return expected_move > required_edge;
    }
};
