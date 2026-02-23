#pragma once
#include <cstdint>

namespace chimera {

enum class RiskMode : uint8_t {
    NORMAL = 0,
    DOWNSCALE = 1,
    PAUSED = 2,
    HARD_KILL = 3
};

}
