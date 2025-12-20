#pragma once

#include "Strategy.hpp"

namespace binance {

class MidPriceStrategy : public Strategy {
public:
    bool on_book(
        const std::string& symbol,
        const OrderBook& book,
        ExecutionIntent& out_intent
    ) override;
};

}
