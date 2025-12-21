#pragma once

#include <atomic>
#include <cstdint>

struct LatencyRegistry {
    // feed → order book (ns)
    std::atomic<int64_t> feed_to_book_ns{0};

    // book → strategy (ns)
    std::atomic<int64_t> book_to_strategy_ns{0};

    // strategy → execution intent (ns)
    std::atomic<int64_t> strategy_to_exec_ns{0};
};

// single global registry
extern LatencyRegistry g_latency;
