#include "execution/ShadowFillEngine.hpp"
#include "forensics/EventTypes.hpp"
#include <cstring>

using namespace chimera;

struct FillEvent {
    char   symbol[16];
    double qty;
    double price;
};
static_assert(sizeof(FillEvent) == 32, "FillEvent must be 32B");

ShadowFillEngine::ShadowFillEngine(Context& ctx)
    : ctx_(ctx) {}

void ShadowFillEngine::on_fill(const std::string& symbol,
                                double qty, double price) {
    FillEvent ev{};
    std::strncpy(ev.symbol, symbol.c_str(), sizeof(ev.symbol) - 1);
    ev.qty   = qty;
    ev.price = price;

    uint64_t causal = ctx_.recorder.next_causal_id();
    ctx_.recorder.write(EventType::FILL, &ev, sizeof(ev), causal);
}

bool ShadowFillEngine::should_fill(const std::string& symbol,
                                    double price, double qty, bool is_buy) {
    // Query queue model for current fill probability.
    // The estimate accounts for depth ahead of us at our price level.
    // Fill threshold: probability >= 0.7 means we're near front of queue.
    // This replaces the previous fixed 0.7 threshold on a raw probability
    // check by ensuring the estimate is actually driven by live book state.
    auto est = ctx_.queue.estimate(symbol, price, qty, is_buy);
    return est.expected_fill_prob >= 0.7;
}
