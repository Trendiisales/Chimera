#pragma once
#include <cstdint>
#include "LatencyTypes.hpp"

namespace chimera {

class CapitalAllocator {
public:
    explicit CapitalAllocator(double max_usd);

    double allocate(double edge, double volatility, LatencyState state);

private:
    double m_max_usd;
};

}
