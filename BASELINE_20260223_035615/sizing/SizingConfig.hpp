#pragma once
#include <cstdint>

namespace chimera {

struct SizingConfig {
    double base_size = 1.0;

    double min_confidence = 0.6;
    double strong_confidence = 0.8;

    uint64_t max_good_rtt_ns = 4'000'000;
    uint64_t max_good_queue_ns = 4'000'000;

    double max_good_slippage_bps = 3.0;

    double low_vol_threshold = 0.8;
    double high_vol_threshold = 2.5;

    double max_scale_up = 1.8;
    double min_scale_down = 0.4;
};

}
