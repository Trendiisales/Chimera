#pragma once
#include <cstdint>

namespace chimera {

struct LossShutdownConfig {
    int max_small_losses = 3;
    uint64_t window_ns = 60'000'000'000ULL;
};

}
