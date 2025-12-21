#pragma once
#include <vector>
#include <memory>

#include "strategy_multi/IMicroStrategy.hpp"
#include "strategy_multi/MultiStrategyTypes.hpp"

namespace Chimera {

class MultiStrategyCoordinator {
public:
    MultiStrategyCoordinator();

    void add(std::unique_ptr<IMicroStrategy> strat);

    StrategyDecision on_signal(
        const AggregatedSignal& sig,
        const RegimeState& regime
    );

private:
    std::vector<std::unique_ptr<IMicroStrategy>> strategies_;
    double decision_threshold_;
};

}
