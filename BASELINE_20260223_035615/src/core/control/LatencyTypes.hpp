#pragma once
#include <cstdint>

namespace chimera {

enum class LatencyState {
    OK = 0,
    DEGRADED = 1,
    KILL = 2
};

}
