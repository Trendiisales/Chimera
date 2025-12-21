#pragma once

#include <atomic>

struct LatencyRegistry {
    std::atomic<long long> feed_to_book_ns{0};
};

extern LatencyRegistry g_latency;
