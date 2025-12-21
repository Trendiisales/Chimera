#pragma once
#include <cstdint>

namespace Chimera {

enum class VolatilityRegime : uint8_t {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2
};

enum class BehaviourRegime : uint8_t {
    TRENDING = 0,
    MEAN_REVERTING = 1,
    NOISY = 2
};

struct alignas(64) RegimeState {
    VolatilityRegime vol;
    BehaviourRegime behaviour;
    double confidence;
    uint64_t ts_ns;
};

}
