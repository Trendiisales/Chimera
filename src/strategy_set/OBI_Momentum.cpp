#include "strategy_set/OBI_Momentum.hpp"

using namespace Chimera;

StrategyDecision OBI_Momentum::on_signal(
    const AggregatedSignal& sig,
    const RegimeState&
) {
    StrategyDecision d{StrategyIntent::FLAT, 0.0, sig.ts_ns};

    if (sig.obi > 0.4) {
        d.intent = StrategyIntent::LONG;
        d.confidence = sig.obi;
    } else if (sig.obi < -0.4) {
        d.intent = StrategyIntent::SHORT;
        d.confidence = -sig.obi;
    }

    return d;
}
