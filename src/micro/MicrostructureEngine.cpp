#include "micro/MicrostructureEngine.hpp"
#include "binance/OrderBook.hpp"

MicrostructureEngine::MicrostructureEngine(
    const std::unordered_map<std::string, binance::OrderBook*>& books
)
: books_(books)
{
    for (const auto& kv : books_) {
        mid_[kv.first] = 0.0;
        spread_bps_[kv.first] = 0.0;
    }
}

void MicrostructureEngine::update() {
    for (const auto& kv : books_) {
        const std::string& symbol = kv.first;
        const binance::OrderBook* book = kv.second;

        if (!book) {
            mid_[symbol] = 0.0;
            spread_bps_[symbol] = 0.0;
            continue;
        }

        double bid = book->best_bid();
        double ask = book->best_ask();

        if (bid <= 0.0 || ask <= 0.0 || ask <= bid) {
            mid_[symbol] = 0.0;
            spread_bps_[symbol] = 0.0;
            continue;
        }

        double m = 0.5 * (bid + ask);
        double spread = ask - bid;

        mid_[symbol] = m;
        spread_bps_[symbol] = (spread / m) * 10000.0;
    }
}

double MicrostructureEngine::mid(const std::string& symbol) const {
    auto it = mid_.find(symbol);
    if (it == mid_.end()) return 0.0;
    return it->second;
}

double MicrostructureEngine::spread_bps(const std::string& symbol) const {
    auto it = spread_bps_.find(symbol);
    if (it == spread_bps_.end()) return 0.0;
    return it->second;
}
