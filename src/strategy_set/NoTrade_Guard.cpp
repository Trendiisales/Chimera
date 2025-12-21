#include "strategy_set/NoTrade_Guard.hpp"

using namespace Chimera;

StrategyDecision NoTrade_Guard::on_signal(
    const AggregatedSignal& sig,
    const RegimeState& regime
) {
    StrategyDecision d{StrategyIntent::FLAT, 1.0, sig.ts_ns};

    if (regime.vol == VolatilityRegime::HIGH &&
        regime.behaviour == BehaviourRegime::NOISY) {
        return d;
    }

    d.confidence = 0.0;
    return d;
}
