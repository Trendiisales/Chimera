#pragma once
#include <string>
#include <vector>

namespace chimera {

struct PostTradeReport {
    uint64_t causal_id = 0;
    std::string entry_reason;
    std::string exit_reason;
    double pnl = 0.0;
    double slippage_bps = 0.0;
    uint64_t decision_to_fill_ns = 0;
};

}
