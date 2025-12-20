#pragma once
#include <cstdint>

namespace Chimera {

enum class StrategyIntent : uint8_t {
    FLAT  = 0,
    LONG  = 1,
    SHORT = 2
};

struct alignas(64) StrategyDecision {
    StrategyIntent intent;
    double confidence;
    uint64_t ts_ns;
};

}
