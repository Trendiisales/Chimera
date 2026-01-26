#pragma once
#include <string>

struct OrderIntent {
    std::string symbol;
    std::string side;     // "BUY" or "SELL"
    double      quantity;
    double      price;    // 0 = market order
    bool        is_market;
};
