#pragma once
#include <cstdint>

namespace Chimera {

struct alignas(64) AggregatedSignal {
    double obi;
    double microprice;
    double flow;
    double volatility;

    double composite;
    uint64_t ts_ns;
};

}
