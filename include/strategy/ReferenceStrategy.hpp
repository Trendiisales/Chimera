#pragma once
#include "signal/SignalTypes.hpp"
#include "strategy/StrategyTypes.hpp"

namespace Chimera {

class ReferenceStrategy {
public:
    ReferenceStrategy();

    StrategyDecision on_signal(const AggregatedSignal& sig);

private:
    StrategyIntent last_intent_;
    uint64_t last_ts_;

    double entry_threshold_;
    double exit_threshold_;
    uint64_t cooldown_ns_;
};

}
