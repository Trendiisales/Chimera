#pragma once
#include <atomic>
#include <cstdint>
#include <chrono>

namespace binance {

/*
 Read-only metrics for Binance engine.
 All fields are atomics. No locks.
*/
struct BinanceMetrics {
    std::atomic<uint64_t> snapshot_attempts{0};
    std::atomic<uint64_t> snapshot_failures{0};
    std::atomic<uint64_t> ws_reconnects{0};
    std::atomic<uint64_t> delta_gaps{0};
    std::atomic<uint64_t> deltas_applied{0};

    std::atomic<uint64_t> last_update_ns{0};

    static uint64_t now_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

}
