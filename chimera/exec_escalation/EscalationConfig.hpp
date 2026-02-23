#pragma once
#include <cstdint>

namespace chimera {

struct EscalationConfig {
    uint64_t min_confirm_ns = 2'000'000;
    uint64_t max_queue_wait_ns = 6'000'000;
    uint64_t max_total_wait_ns = 12'000'000;

    double min_signal_confidence = 0.65;
    double min_volatility = 1.1;

    uint64_t max_rtt_ns = 5'000'000;
};

}
