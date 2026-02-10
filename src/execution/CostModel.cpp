#include "risk/CostModel.hpp"

CostBreakdown CostModel::compute(
    const std::string& symbol,
    double bid,
    double ask,
    double lot_size,
    double latency_ms
) {
    CostBreakdown c{};
    
    const double mid = 0.5 * (bid + ask);
    const double spread = ask - bid;
    
    double commission_per_lot = 0.0;
    if (symbol == "XAUUSD") {
        commission_per_lot = 6.0;
    } else if (symbol == "XAGUSD") {
        commission_per_lot = 5.0;
    }
    
    c.commission_cost = (commission_per_lot * lot_size) / mid;
    c.spread_cost = spread;
    c.slippage_cost = spread * 0.25;
    
    const double latency_penalty_bps = (symbol == "XAUUSD") ? 0.8 : 1.2;
    c.latency_cost = mid * (latency_penalty_bps / 10000.0) * (latency_ms / 10.0);
    
    c.total_cost = c.spread_cost + c.commission_cost + c.slippage_cost + c.latency_cost;
    
    return c;
}
