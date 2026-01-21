#pragma once

#include "chimera/safety/ExchangeInfoCache.hpp"

namespace chimera {

class PrecisionEngine {
public:
    explicit PrecisionEngine(
        ExchangeInfoCache& cache
    );

    bool normalize(
        const std::string& symbol,
        double& qty,
        double& price
    ) const;

private:
    double roundDown(
        double value,
        double step
    ) const;

private:
    ExchangeInfoCache& exinfo;
};

}
