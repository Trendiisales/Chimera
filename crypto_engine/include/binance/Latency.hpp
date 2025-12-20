#pragma once
#include <cstdint>
#include <atomic>
#include <limits>
#include <chrono>

namespace binance {

inline uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

struct LatencyStats {
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> sum_ns{0};
    std::atomic<uint64_t> min_ns{std::numeric_limits<uint64_t>::max()};
    std::atomic<uint64_t> max_ns{0};

    void record(uint64_t ns) {
        count.fetch_add(1, std::memory_order_relaxed);
        sum_ns.fetch_add(ns, std::memory_order_relaxed);

        uint64_t cur_min = min_ns.load(std::memory_order_relaxed);
        while (ns < cur_min &&
               !min_ns.compare_exchange_weak(cur_min, ns, std::memory_order_relaxed)) {}

        uint64_t cur_max = max_ns.load(std::memory_order_relaxed);
        while (ns > cur_max &&
               !max_ns.compare_exchange_weak(cur_max, ns, std::memory_order_relaxed)) {}
    }

    uint64_t avg_ns() const {
        uint64_t c = count.load(std::memory_order_relaxed);
        if (c == 0) return 0;
        return sum_ns.load(std::memory_order_relaxed) / c;
    }
};

}
