#pragma once
#include "signal/SignalTypes.hpp"
#include "regime/RegimeTypes.hpp"
#include "strategy/StrategyTypes.hpp"

namespace Chimera {

// Strategy interface — all strategies conform to this
class IMicroStrategy {
public:
    virtual ~IMicroStrategy() = default;

    virtual StrategyDecision on_signal(
        const AggregatedSignal& sig,
        const RegimeState& regime
    ) = 0;
};

}
