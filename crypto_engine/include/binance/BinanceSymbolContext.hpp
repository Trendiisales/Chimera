#pragma once
#include "BinanceDeltaGate.hpp"
#include "BinanceOrderBookSoA.hpp"
#include "BinanceHotState.hpp"
#include <string>

namespace binance {

struct SymbolContext {
    std::string symbol;
    DeltaGate gate;
    OrderBookSoA book;
    HotFeedState hot;

    explicit SymbolContext(const std::string& s)
        : symbol(s) {}
};

}
