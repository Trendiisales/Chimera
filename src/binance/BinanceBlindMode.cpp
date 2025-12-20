#include "binance/BinanceBlindMode.hpp"

using namespace Chimera;

static constexpr uint64_t DEPTH_TIMEOUT_NS = 50ULL * 1000 * 1000;
static constexpr uint32_t MAX_MISSES = 3;

BinanceBlindMode::BinanceBlindMode(BlindModeState& state)
    : state_(state) {}

void BinanceBlindMode::on_depth_update(uint64_t ts_ns) {
    state_.last_depth_ts_ns.store(ts_ns, std::memory_order_relaxed);
    state_.miss_count.store(0, std::memory_order_relaxed);

    if (state_.active.load(std::memory_order_relaxed)) {
        state_.recover_count.fetch_add(1, std::memory_order_relaxed);
        if (state_.recover_count.load(std::memory_order_relaxed) >= 5) {
            state_.active.store(false, std::memory_order_relaxed);
            state_.recover_count.store(0, std::memory_order_relaxed);
        }
    }
}

bool BinanceBlindMode::should_blind(uint64_t now_ns) {
    uint64_t last = state_.last_depth_ts_ns.load(std::memory_order_relaxed);
    if (now_ns > last + DEPTH_TIMEOUT_NS) {
        uint32_t misses = state_.miss_count.fetch_add(1, std::memory_order_relaxed);
        if (misses >= MAX_MISSES) {
            state_.active.store(true, std::memory_order_relaxed);
            return true;
        }
    }
    return state_.active.load(std::memory_order_relaxed);
}

void BinanceBlindMode::on_trade_attempt() {
    if (state_.active.load(std::memory_order_relaxed)) {
        state_.miss_count.fetch_add(1, std::memory_order_relaxed);
    }
}

double BinanceBlindMode::widen_price(double px, bool is_bid) const {
    if (!state_.active.load(std::memory_order_relaxed)) return px;
    return is_bid ? px * 0.9995 : px * 1.0005;
}

double BinanceBlindMode::cap_qty(double qty) const {
    if (!state_.active.load(std::memory_order_relaxed)) return qty;
    return qty * 0.25;
}
