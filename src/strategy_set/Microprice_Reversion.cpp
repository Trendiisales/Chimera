#include "strategy_set/Microprice_Reversion.hpp"
#include <cmath>

using namespace Chimera;

StrategyDecision Microprice_Reversion::on_signal(
    const AggregatedSignal& sig,
    const RegimeState&
) {
    StrategyDecision d{StrategyIntent::FLAT, 0.0, sig.ts_ns};

    if (std::abs(sig.microprice) > 0.6) {
        d.intent = (sig.microprice > 0)
            ? StrategyIntent::SHORT
            : StrategyIntent::LONG;
        d.confidence = std::min(1.0, std::abs(sig.microprice));
    }

    return d;
}
