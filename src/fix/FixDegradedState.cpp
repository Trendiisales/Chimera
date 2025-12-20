#include "fix/FixDegradedState.hpp"

using namespace Chimera;

static constexpr uint32_t MAX_REJECTS   = 3;
static constexpr uint32_t MAX_TIMEOUTS  = 2;
static constexpr uint64_t MAX_LATENCY_US = 8000;
static constexpr uint64_t RX_STALL_NS   = 200ULL * 1000 * 1000;

FixDegradedState::FixDegradedState() {
    state_.store(FixState::DISCONNECTED, std::memory_order_relaxed);
}

void FixDegradedState::on_connect() {
    state_.store(FixState::CONNECTING, std::memory_order_relaxed);
}

void FixDegradedState::on_logon() {
    metrics_.reject_count.store(0, std::memory_order_relaxed);
    metrics_.timeout_count.store(0, std::memory_order_relaxed);
    state_.store(FixState::LOGGED_IN, std::memory_order_relaxed);
}

void FixDegradedState::on_disconnect() {
    state_.store(FixState::DISCONNECTED, std::memory_order_relaxed);
}

void FixDegradedState::on_rx(uint64_t now_ns) {
    metrics_.last_rx_ns.store(now_ns, std::memory_order_relaxed);
    update_state();
}

void FixDegradedState::on_tx(uint64_t now_ns) {
    metrics_.last_tx_ns.store(now_ns, std::memory_order_relaxed);
}

void FixDegradedState::on_latency(uint64_t latency_us) {
    uint64_t ema = metrics_.latency_us_ema.load(std::memory_order_relaxed);
    ema = (ema * 7 + latency_us) / 8;
    metrics_.latency_us_ema.store(ema, std::memory_order_relaxed);
    update_state();
}

void FixDegradedState::on_reject() {
    metrics_.reject_count.fetch_add(1, std::memory_order_relaxed);
    update_state();
}

void FixDegradedState::on_timeout() {
    metrics_.timeout_count.fetch_add(1, std::memory_order_relaxed);
    update_state();
}

FixState FixDegradedState::state() const {
    return state_.load(std::memory_order_relaxed);
}

bool FixDegradedState::allow_new_orders() const {
    FixState s = state_.load(std::memory_order_relaxed);
    return s == FixState::LOGGED_IN || s == FixState::DEGRADED;
}

double FixDegradedState::size_multiplier() const {
    FixState s = state_.load(std::memory_order_relaxed);
    if (s == FixState::DEGRADED) return 0.25;
    if (s == FixState::HALTED) return 0.0;
    return 1.0;
}

void FixDegradedState::update_state() {
    FixState s = state_.load(std::memory_order_relaxed);

    uint32_t rejects  = metrics_.reject_count.load(std::memory_order_relaxed);
    uint32_t timeouts = metrics_.timeout_count.load(std::memory_order_relaxed);
    uint64_t latency  = metrics_.latency_us_ema.load(std::memory_order_relaxed);

    uint64_t last_rx = metrics_.last_rx_ns.load(std::memory_order_relaxed);

    if (s == FixState::LOGGED_IN) {
        if (rejects >= MAX_REJECTS || timeouts >= MAX_TIMEOUTS || latency > MAX_LATENCY_US) {
            state_.store(FixState::DEGRADED, std::memory_order_relaxed);
        }
    }

    if (s == FixState::DEGRADED) {
        if (rejects >= MAX_REJECTS * 2 || timeouts >= MAX_TIMEOUTS * 2) {
            state_.store(FixState::HALTED, std::memory_order_relaxed);
        }
    }

    if (last_rx != 0) {
        uint64_t now_ns = last_rx;
        if (now_ns - last_rx > RX_STALL_NS) {
            state_.store(FixState::HALTED, std::memory_order_relaxed);
        }
    }
}
