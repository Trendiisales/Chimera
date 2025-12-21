#include "micro/MicrostructureEngine.hpp"
#include "binance/OrderBook.hpp"

MicrostructureEngine::MicrostructureEngine(
    const std::unordered_map<std::string, binance::OrderBook*>& books
)
: books_(books) {}

void MicrostructureEngine::update() {
    for (const auto& kv : books_) {
        const std::string& symbol = kv.first;
        binance::OrderBook* book = kv.second;

        auto& snap = snaps_[symbol];

        if (book->empty()) {
            snap.mid.store(0.0, std::memory_order_relaxed);
            snap.spread.store(0.0, std::memory_order_relaxed);
            snap.spread_bps.store(0.0, std::memory_order_relaxed);
            continue;
        }

        double bid = book->best_bid();
        double ask = book->best_ask();

        if (bid <= 0.0 || ask <= 0.0 || ask <= bid) {
            continue;
        }

        double mid = 0.5 * (bid + ask);
        double spread = ask - bid;
        double spread_bps = (spread / mid) * 10000.0;

        snap.mid.store(mid, std::memory_order_relaxed);
        snap.spread.store(spread, std::memory_order_relaxed);
        snap.spread_bps.store(spread_bps, std::memory_order_relaxed);
    }
}

double MicrostructureEngine::mid(const std::string& symbol) const {
    auto it = snaps_.find(symbol);
    return (it != snaps_.end())
        ? it->second.mid.load(std::memory_order_relaxed)
        : 0.0;
}

double MicrostructureEngine::spread_bps(const std::string& symbol) const {
    auto it = snaps_.find(symbol);
    return (it != snaps_.end())
        ? it->second.spread_bps.load(std::memory_order_relaxed)
        : 0.0;
}
