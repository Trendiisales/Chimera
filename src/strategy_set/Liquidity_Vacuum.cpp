#include "strategy_set/Liquidity_Vacuum.hpp"
#include <cmath>

using namespace Chimera;

StrategyDecision Liquidity_Vacuum::on_signal(
    const AggregatedSignal& sig,
    const RegimeState&
) {
    StrategyDecision d{StrategyIntent::FLAT, 0.0, sig.ts_ns};

    if (sig.volatility > 0.5 && std::abs(sig.obi) < 0.1) {
        d.intent = (sig.composite > 0)
            ? StrategyIntent::LONG
            : StrategyIntent::SHORT;
        d.confidence = sig.volatility;
    }

    return d;
}
