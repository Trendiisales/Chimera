#pragma once

#include "strategy_multi/IMicroStrategy.hpp"
#include "regime/RegimeTypes.hpp"

namespace Chimera {

class NoTrade_Guard final : public IMicroStrategy {
public:
    StrategyDecision on_signal(
        const AggregatedSignal& sig,
        const RegimeState& regime
    ) override;
};

}
