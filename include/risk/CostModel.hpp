#pragma once

#include <string>

struct CostBreakdown {
    double spread_cost = 0.0;
    double commission_cost = 0.0;
    double slippage_cost = 0.0;
    double latency_cost = 0.0;
    double total_cost = 0.0;
};

class CostModel {
public:
    static CostBreakdown compute(
        const std::string& symbol,
        double entry_price,
        double exit_price,
        double quantity,
        double commission_per_lot
    );
};
