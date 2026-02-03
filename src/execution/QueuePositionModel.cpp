#include "execution/QueuePositionModel.hpp"
#include <cmath>

using namespace chimera;

void QueuePositionModel::on_book_update(const std::string& symbol,
                                         double bid_price, double bid_depth,
                                         double ask_price, double ask_depth,
                                         uint64_t ts_ns) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto& b = books_[symbol];
    b.bid_price      = bid_price;
    b.ask_price      = ask_price;
    b.bid_depth      = bid_depth;
    b.ask_depth      = ask_depth;
    b.last_update_ns = ts_ns;
}

TopOfBook QueuePositionModel::top(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mtx_);
    TopOfBook tb;
    auto it = books_.find(symbol);
    if (it == books_.end()) { tb.valid = false; return tb; }
    const QueueState& b = it->second;
    tb.bid      = b.bid_price;
    tb.ask      = b.ask_price;
    tb.bid_size = b.bid_depth;
    tb.ask_size = b.ask_depth;
    tb.ts_ns    = b.last_update_ns;
    tb.valid    = (b.last_update_ns != 0);
    return tb;
}

OrderQueueEstimate QueuePositionModel::estimate(const std::string& symbol,
                                                 double price, double qty,
                                                 bool is_buy) {
    std::lock_guard<std::mutex> lock(mtx_);
    OrderQueueEstimate est;

    auto it = books_.find(symbol);
    if (it == books_.end()) { est.expected_fill_prob = 0.0; return est; }

    const QueueState& b = it->second;

    if (is_buy)  est.ahead_qty = (price < b.ask_price) ? b.ask_depth : 0.0;
    else         est.ahead_qty = (price > b.bid_price) ? b.bid_depth : 0.0;

    est.behind_qty = qty;
    double pressure = 1.0 / (1.0 + est.ahead_qty);
    est.expected_fill_prob = std::min(1.0, pressure * 0.85);
    return est;
}

std::unordered_map<std::string, QueueState> QueuePositionModel::dump_books() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return books_;
}

void QueuePositionModel::clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    books_.clear();
}

void QueuePositionModel::restore(const std::string& sym, const QueueState& st) {
    std::lock_guard<std::mutex> lock(mtx_);
    books_[sym] = st;
}
