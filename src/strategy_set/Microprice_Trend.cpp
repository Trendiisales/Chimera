#include "strategy_set/Microprice_Trend.hpp"

using namespace Chimera;

StrategyDecision Microprice_Trend::on_signal(
    const AggregatedSignal& sig,
    const RegimeState&
) {
    StrategyDecision d{StrategyIntent::FLAT, 0.0, sig.ts_ns};

    if (sig.microprice > 0.3) {
        d.intent = StrategyIntent::LONG;
        d.confidence = sig.microprice;
    } else if (sig.microprice < -0.3) {
        d.intent = StrategyIntent::SHORT;
        d.confidence = -sig.microprice;
    }

    return d;
}
