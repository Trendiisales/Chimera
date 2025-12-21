#include "strategy/MeanReversionStrategy.hpp"
#include <chrono>

static inline std::uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void MeanReversionStrategy::tick() {
    if ((++n_ % 8) == 0) {
        ctx_.intents.push(Intent(
            (n_ & 1) ? Intent::BUY : Intent::SELL,
            "BTCUSDT",
            1.0,
            now_ns()
        ));
    }
}
