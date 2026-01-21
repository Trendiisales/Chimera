#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace chimera {

struct TradeRecord {
    std::string engine;
    std::string symbol;
    bool is_buy = false;
    double qty = 0.0;
    double entry = 0.0;
    double exit = 0.0;
    double pnl = 0.0;
    uint64_t open_ts = 0;
    uint64_t close_ts = 0;
};

struct DecisionTrace {
    std::string engine;
    bool approved = false;
    std::string block_reason;
    double edge_score = 0.0;
    double spread = 0.0;
    double expected_cost = 0.0;
};

class Telemetry {
public:
    void logTrade(const TradeRecord& rec);
    void logDecision(const DecisionTrace& trace);

    const std::vector<TradeRecord>& trades() const;
    const std::vector<DecisionTrace>& decisions() const;

private:
    std::vector<TradeRecord> trade_log;
    std::vector<DecisionTrace> decision_log;
};

}
