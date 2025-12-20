#pragma once
#include <atomic>
#include "LowLatency.hpp"

namespace binance {

/*
 Hot state touched on every delta.
 Aligned to avoid false sharing.
*/
struct CACHE_ALIGN HotFeedState {
    std::atomic<uint64_t> deltas_applied{0};
    std::atomic<uint64_t> gaps_detected{0};
};

}
