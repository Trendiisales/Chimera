#pragma once
#include <atomic>

struct LatencyStats {
    std::atomic<double> last_ms{0};
};

extern LatencyStats g_latency;
