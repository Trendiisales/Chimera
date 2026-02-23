#pragma once
#include <cstdint>

namespace chimera {

struct ExecPolicyConfig {
    uint64_t max_rtt_ns = 5'000'000;
    uint64_t max_queue_wait_ns = 10'000'000;

    double max_reject_rate = 0.15;
    double max_spread_bps = 6.0;
    double vol_burst_threshold = 3.0;

    double size_downscale = 0.5;
    double size_upscale = 1.0;

    uint64_t hard_kill_cooldown_ns = 60'000'000'000ULL;
};

}
