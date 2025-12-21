#include "execution/ExecutionEngine.hpp"
#include "risk/RiskManager.hpp"
#include "execution/PositionTracker.hpp"

ExecutionEngine::ExecutionEngine(
    RiskManager& risk,
    PositionTracker& positions
)
: risk_(risk), positions_(positions) {}

void ExecutionEngine::submit_intent(
    const std::string& symbol,
    const std::string& side,
    double price,
    double qty
) {
    intents_.fetch_add(1, std::memory_order_relaxed);

    Fill f{symbol, side, price, qty};

    positions_.on_fill(f);
    risk_.on_fill(f);
}

uint64_t ExecutionEngine::intents_seen() const {
    return intents_.load(std::memory_order_relaxed);
}
