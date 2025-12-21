#include "strategy_set/Vol_Expansion.hpp"
#include <cmath>

using namespace Chimera;

StrategyDecision Vol_Expansion::on_signal(
    const AggregatedSignal& sig,
    const RegimeState&
) {
    StrategyDecision d{StrategyIntent::FLAT, 0.0, sig.ts_ns};

    if (sig.volatility > 0.7 && std::abs(sig.composite) > 0.3) {
        d.intent = (sig.composite > 0)
            ? StrategyIntent::LONG
            : StrategyIntent::SHORT;
        d.confidence = sig.volatility;
    }

    return d;
}
