#include "binance/MidPriceStrategy.hpp"

#include <chrono>

namespace binance {

static uint64_t now_ns() {
    using namespace std::chrono;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool MidPriceStrategy::on_book(
    const std::string& symbol,
    const OrderBook& book,
    ExecutionIntent& out_intent) {

    if (book.bids_empty() || book.asks_empty())
        return false;

    double bid = book.best_bid();
    double ask = book.best_ask();
    double mid = (bid + ask) * 0.5;

    out_intent.symbol = symbol;
    out_intent.side = Side::FLAT;
    out_intent.price = mid;
    out_intent.quantity = 0.0;
    out_intent.ts_ns = now_ns();

    return true;
}

}
