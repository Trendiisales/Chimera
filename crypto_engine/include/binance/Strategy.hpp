#pragma once

#include "ExecutionTypes.hpp"
#include "BinanceOrderBook.hpp"

namespace binance {

class Strategy {
public:
    virtual ~Strategy() = default;

    virtual bool on_book(
        const std::string& symbol,
        const OrderBook& book,
        ExecutionIntent& out_intent
    ) = 0;
};

}
