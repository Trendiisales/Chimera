#include "strategy_multi/MultiStrategyCoordinator.hpp"
#include <cmath>

using namespace Chimera;

MultiStrategyCoordinator::MultiStrategyCoordinator()
    : decision_threshold_(0.5) {}

void MultiStrategyCoordinator::add(std::unique_ptr<IMicroStrategy> strat) {
    strategies_.push_back(std::move(strat));
}

StrategyDecision MultiStrategyCoordinator::on_signal(
    const AggregatedSignal& sig,
    const RegimeState& regime
) {
    double sum = 0.0;

    for (const auto& s : strategies_) {
        StrategyDecision d = s->on_signal(sig, regime);

        int dir = 0;
        if (d.intent == StrategyIntent::LONG)  dir = 1;
        if (d.intent == StrategyIntent::SHORT) dir = -1;

        sum += dir * d.confidence;
    }

    StrategyDecision out{};
    out.ts_ns = sig.ts_ns;
    out.confidence = std::min(1.0, std::abs(sum));

    if (sum > decision_threshold_) {
        out.intent = StrategyIntent::LONG;
    } else if (sum < -decision_threshold_) {
        out.intent = StrategyIntent::SHORT;
    } else {
        out.intent = StrategyIntent::FLAT;
    }

    return out;
}
