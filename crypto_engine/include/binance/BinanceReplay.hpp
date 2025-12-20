#pragma once

#include "BinanceOrderBook.hpp"
#include "BinanceDeltaGate.hpp"
#include "BinanceDepthParser.hpp"

#include <string>

namespace binance {

/*
 Offline deterministic replay harness.
 Replays WS depth messages line-by-line.
*/
class BinanceReplay {
public:
    BinanceReplay(OrderBook& book, DeltaGate& gate);

    bool replay_file(const std::string& path);

private:
    OrderBook& book;
    DeltaGate& gate;
};

}
