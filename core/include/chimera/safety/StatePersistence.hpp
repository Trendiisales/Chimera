#pragma once

#include <string>

#include "chimera/execution/PositionBook.hpp"
#include "chimera/governance/StrategyFitnessEngine.hpp"
#include "chimera/governance/CorrelationGovernor.hpp"

namespace chimera {

class StatePersistence {
public:
    explicit StatePersistence(
        const std::string& path
    );

    void save(
        const PositionBook& book,
        const StrategyFitnessEngine& fitness,
        const CorrelationGovernor& corr
    );

    void load(
        PositionBook& book,
        StrategyFitnessEngine& fitness,
        CorrelationGovernor& corr
    );

private:
    std::string file;
};

}
