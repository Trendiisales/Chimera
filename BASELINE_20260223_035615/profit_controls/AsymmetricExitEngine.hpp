#pragma once
#include "ExitExecDecision.hpp"

namespace chimera {

class AsymmetricExitEngine {
public:
    ExitExecDecision decide(uint64_t causal_id,
                            uint64_t now_ns,
                            double unrealized_pnl,
                            uint64_t rtt_ns,
                            double volatility);
};

}
