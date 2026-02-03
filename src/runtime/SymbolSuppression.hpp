#pragma once
#include <atomic>
#include <cstdint>

namespace chimera {

struct SymbolSuppression {
    static constexpr uint64_t MAX_SYMBOLS = 16;

    std::atomic<uint64_t> blocked_until_ns[MAX_SYMBOLS];

    SymbolSuppression() {
        for (uint64_t i = 0; i < MAX_SYMBOLS; ++i)
            blocked_until_ns[i].store(0, std::memory_order_relaxed);
    }

    inline bool is_blocked(uint64_t symbol_id, uint64_t now_ns) const {
        return now_ns < blocked_until_ns[symbol_id].load(std::memory_order_relaxed);
    }

    inline void block(uint64_t symbol_id, uint64_t now_ns, uint64_t duration_ns) {
        blocked_until_ns[symbol_id].store(now_ns + duration_ns, std::memory_order_relaxed);
    }

    inline void clear(uint64_t symbol_id) {
        blocked_until_ns[symbol_id].store(0, std::memory_order_relaxed);
    }
};

}
