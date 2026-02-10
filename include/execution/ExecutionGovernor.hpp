#pragma once
#include <string>
#include <cstdint>
#include "risk/CostModel.hpp"
#include "core/TradeLedger.hpp"

struct ExecutionDecision {
    bool allowed;
    double size;
    double expected_edge;
    double total_cost;
    const char* block_reason;
};

class ExecutionGovernor {
public:
    ExecutionGovernor(TradeLedger& ledger);
    ExecutionDecision evaluate_entry(const std::string& symbol, double bid, double ask,
                                      double base_size, double expected_edge, double latency_ms);
    uint64_t commit_entry(const std::string& symbol, TradeSide side, double size, double price,
                          double bid, double ask, double latency_ms, uint64_t ts);
    void commit_exit(uint64_t trade_id, double exit_price, uint64_t ts);
private:
    TradeLedger& ledger_;
};
