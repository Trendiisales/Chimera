#include "strategy/HeartbeatStrategy.hpp"
#include <chrono>

static inline std::uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void HeartbeatStrategy::tick() {
    if ((++n_ % 10) == 0) {
        q_.push(Intent(
            Intent::BUY,
            "BTCUSDT",
            1.0,
            now_ns()
        ));
    }
}
