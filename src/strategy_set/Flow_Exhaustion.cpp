#include "strategy_set/Flow_Exhaustion.hpp"
#include <cmath>

using namespace Chimera;

StrategyDecision Flow_Exhaustion::on_signal(
    const AggregatedSignal& sig,
    const RegimeState&
) {
    StrategyDecision d{StrategyIntent::FLAT, 0.0, sig.ts_ns};

    if (std::abs(sig.flow) < 0.1 && sig.volatility > 0.3) {
        d.intent = (sig.flow >= 0)
            ? StrategyIntent::SHORT
            : StrategyIntent::LONG;
        d.confidence = sig.volatility;
    }

    return d;
}
