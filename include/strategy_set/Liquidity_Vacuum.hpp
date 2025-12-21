#pragma once

#include "strategy_multi/IMicroStrategy.hpp"

namespace Chimera {

class Liquidity_Vacuum final : public IMicroStrategy {
public:
    StrategyDecision on_signal(
        const AggregatedSignal& sig,
        const RegimeState& regime
    ) override;
};

}
