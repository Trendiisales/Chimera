#include "execution/OrderBookView.hpp"
#include <unordered_map>

namespace chimera {

void OrderBookView::update(const std::string& symbol,
                           double bid,
                           double ask,
                           double bid_depth,
                           double ask_depth) {
    auto& e = m_books[symbol];
    e.bid = bid;
    e.ask = ask;
    e.bid_depth = bid_depth;
    e.ask_depth = ask_depth;
}

BookTop OrderBookView::top(const std::string& symbol) const {
    BookTop out;
    auto it = m_books.find(symbol);
    if (it == m_books.end()) return out;

    out.bid = it->second.bid;
    out.ask = it->second.ask;
    out.bid_depth = it->second.bid_depth;
    out.ask_depth = it->second.ask_depth;
    return out;
}

}
