#include "execution/ExecutionEngine.hpp"
#include <chrono>

using clock_type = std::chrono::steady_clock;

ExecutionEngine::ExecutionEngine(
    RiskManager& risk,
    PositionTracker& positions,
    CostModel& costs
)
: risk_(risk), positions_(positions), costs_(costs) {}

void ExecutionEngine::set_fill_callback(FillCallback cb) {
    on_fill_ = std::move(cb);
}

void ExecutionEngine::submit_intent(
    const std::string& symbol,
    const std::string& side,
    double price,
    double qty
) {
    intents_.fetch_add(1, std::memory_order_relaxed);

    if (side == "BUY") {
        emit_fill(symbol, +qty, price);
    } else if (side == "SELL") {
        emit_fill(symbol, -qty, price);
    }
}

uint64_t ExecutionEngine::intents_seen() const {
    return intents_.load(std::memory_order_relaxed);
}

void ExecutionEngine::emit_fill(
    const std::string& symbol,
    double qty,
    double price
) {
    if (!on_fill_) return;

    int64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock_type::now().time_since_epoch()
    ).count();

    Fill f {
        symbol,
        qty,
        price,
        0.0,
        ts
    };

    on_fill_(f);
}
