#pragma once
#include <cstdint>

namespace Chimera {

enum class RiskVerdict : uint8_t {
    BLOCK = 0,
    ALLOW = 1
};

struct alignas(64) RiskDecision {
    RiskVerdict verdict;
    double risk_multiplier;
    uint64_t ts_ns;
};

}
