#include "strategy_set/Vol_Compression.hpp"
#include <cmath>

using namespace Chimera;

StrategyDecision Vol_Compression::on_signal(
    const AggregatedSignal& sig,
    const RegimeState&
) {
    StrategyDecision d{StrategyIntent::FLAT, 0.0, sig.ts_ns};

    if (sig.volatility < 0.15 && std::abs(sig.composite) > 0.4) {
        d.intent = (sig.composite > 0)
            ? StrategyIntent::LONG
            : StrategyIntent::SHORT;
        d.confidence = std::abs(sig.composite);
    }

    return d;
}
