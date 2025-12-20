#pragma once
#include <cstdint>

namespace Chimera {

struct alignas(64) MicroSignal {
    double value;
    uint64_t ts_ns;
};

}
