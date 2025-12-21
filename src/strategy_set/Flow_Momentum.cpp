#include "strategy_set/Flow_Momentum.hpp"

using namespace Chimera;

StrategyDecision Flow_Momentum::on_signal(
    const AggregatedSignal& sig,
    const RegimeState&
) {
    StrategyDecision d{StrategyIntent::FLAT, 0.0, sig.ts_ns};

    if (sig.flow > 0.5) {
        d.intent = StrategyIntent::LONG;
        d.confidence = sig.flow;
    } else if (sig.flow < -0.5) {
        d.intent = StrategyIntent::SHORT;
        d.confidence = -sig.flow;
    }

    return d;
}
