#include "strategy_set/OBI_Reversion.hpp"
#include <cmath>

using namespace Chimera;

StrategyDecision OBI_Reversion::on_signal(
    const AggregatedSignal& sig,
    const RegimeState&
) {
    StrategyDecision d{StrategyIntent::FLAT, 0.0, sig.ts_ns};

    if (std::abs(sig.obi) > 0.8) {
        d.intent = (sig.obi > 0)
            ? StrategyIntent::SHORT
            : StrategyIntent::LONG;
        d.confidence = std::min(1.0, std::abs(sig.obi));
    }

    return d;
}
