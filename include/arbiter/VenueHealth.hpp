#pragma once
#include <atomic>
#include <cstdint>

namespace Chimera {

struct alignas(64) VenueHealth {
    std::atomic<bool> ws_up;
    std::atomic<uint64_t> latency_us_ema;
    std::atomic<uint32_t> reject_rate;
    std::atomic<bool> throttled;

    VenueHealth() {
        ws_up.store(false);
        latency_us_ema.store(0);
        reject_rate.store(0);
        throttled.store(false);
    }
};

}
