#pragma once
#include <cstdint>

namespace Chimera {

struct alignas(64) StrategyVote {
    int direction;        // -1 short, 0 flat, +1 long
    double confidence;    // 0.0 - 1.0
};

}
