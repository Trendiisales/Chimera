#include "AsymmetricExitEngine.hpp"

namespace chimera {

ExitExecDecision AsymmetricExitEngine::decide(uint64_t causal_id,
                                              uint64_t now_ns,
                                              double unrealized_pnl,
                                              uint64_t rtt_ns,
                                              double volatility) {
    ExitExecDecision d{};
    d.causal_id = causal_id;
    d.ts_ns = now_ns;

    if (unrealized_pnl > 0 &&
        (rtt_ns > 6'000'000 || volatility > 2.5)) {
        d.mode = ExitExecMode::AGGRESSIVE_TAKER;
    }

    return d;
}

}
