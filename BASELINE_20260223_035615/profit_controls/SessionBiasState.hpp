#pragma once
#include <cstdint>

namespace chimera {

struct SessionBiasState {
    double multiplier = 1.0;
    uint64_t bucket_start_ns = 0;
};

}
