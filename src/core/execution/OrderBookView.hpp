#pragma once
#include <string>
#include <unordered_map>

namespace chimera {

struct BookTop {
    double bid = 0.0;
    double ask = 0.0;
    double bid_depth = 0.0;
    double ask_depth = 0.0;
};

class OrderBookView {
public:
    void update(const std::string& symbol,
                double bid,
                double ask,
                double bid_depth,
                double ask_depth);

    BookTop top(const std::string& symbol) const;

private:
    struct Entry {
        double bid = 0.0;
        double ask = 0.0;
        double bid_depth = 0.0;
        double ask_depth = 0.0;
    };

    std::unordered_map<std::string, Entry> m_books;
};

}
