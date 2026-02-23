#pragma once
#include <cstdint>

namespace chimera {

struct CapitalConfig {
    double max_daily_drawdown = -500.0;
    double soft_drawdown = -200.0;

    double downscale_factor = 0.5;
    double upscale_factor = 1.0;

    uint64_t stability_window_ns = 300'000'000'000ULL;
};

}
