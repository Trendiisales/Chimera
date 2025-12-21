#pragma once
#include <string>

struct Fill {
    std::string symbol;
    std::string side;   // BUY / SELL
    double price;
    double qty;
};
