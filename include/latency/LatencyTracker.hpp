#pragma once

#include <atomic>
#include <chrono>

class LatencyTracker {
public:
    void observe_ns(long long ns) {
        last_ns.store(ns, std::memory_order_relaxed);
    }

    long long last() const {
        return last_ns.load(std::memory_order_relaxed);
    }

private:
    std::atomic<long long> last_ns{0};
};
