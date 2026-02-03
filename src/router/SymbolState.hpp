#pragma once
#include <atomic>
#include <cstdint>

namespace chimera {

struct USDSymbolState {
    std::atomic<double> position{0.0};
    std::atomic<double> last_price{0.0};
    std::atomic<uint64_t> suppress_until_ns{0};

    inline void update_fill(double delta, double price, uint64_t now_ns) {
        position.fetch_add(delta, std::memory_order_relaxed);
        last_price.store(price, std::memory_order_relaxed);
    }

    inline bool is_suppressed(uint64_t now_ns) const {
        return now_ns < suppress_until_ns.load(std::memory_order_relaxed);
    }
};

}
