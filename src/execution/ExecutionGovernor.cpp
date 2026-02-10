#include "execution/ExecutionGovernor.hpp"

ExecutionGovernor::ExecutionGovernor(TradeLedger& ledger) : ledger_(ledger) {}

ExecutionDecision ExecutionGovernor::evaluate_entry(const std::string& symbol, double bid, double ask,
    double base_size, double expected_edge, double latency_ms) {
    ExecutionDecision d{};
    d.allowed = false;
    d.size = 0.0;
    d.expected_edge = expected_edge;
    d.total_cost = 0.0;
    d.block_reason = nullptr;

    CostBreakdown cost = CostModel::compute(symbol, bid, ask, base_size, latency_ms);
    d.total_cost = cost.total_cost;

    if (expected_edge < cost.total_cost) {
        d.block_reason = "EDGE_LT_COST";
        return d;
    }

    double spread = ask - bid;
    if (spread > 0.30) {
        d.block_reason = "SPREAD_EXPLOSION";
        return d;
    }

    d.size = base_size;
    d.allowed = true;
    return d;
}

uint64_t ExecutionGovernor::commit_entry(const std::string& symbol, TradeSide side, double size,
    double price, double bid, double ask, double latency_ms, uint64_t ts) {
    uint64_t trade_id = ledger_.open_trade(symbol, side, size, price, ts);
    CostBreakdown cost = CostModel::compute(symbol, bid, ask, size, latency_ms);
    ledger_.apply_costs(trade_id, cost.spread_cost, cost.commission_cost, cost.latency_cost);
    return trade_id;
}

void ExecutionGovernor::commit_exit(uint64_t trade_id, double exit_price, uint64_t ts) {
    ledger_.close_trade(trade_id, exit_price, ts);
}
