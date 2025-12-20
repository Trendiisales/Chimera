#pragma once
#include <cstdint>

namespace Chimera {

enum class ShadowSource : uint8_t {
    LIVE = 1,
    REPLAY = 2
};

struct alignas(64) DecisionSnapshot {
    uint64_t seq;
    uint8_t  source;
    uint8_t  allow;
    double   size_mult;
};

}
