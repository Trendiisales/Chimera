#pragma once
#include <cstdint>

namespace Chimera {

struct alignas(64) CapitalDecision {
    bool allow;
    double notional_mult;
    uint64_t ts_ns;
};

}
