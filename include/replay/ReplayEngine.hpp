#pragma once

#include <string>

namespace binance {
class OrderBook;
}

class MicrostructureEngine;
class StrategyEngine;

class ReplayEngine {
public:
    ReplayEngine(
        binance::OrderBook& book,
        MicrostructureEngine& micro,
        StrategyEngine& strategies
    );

    void run(const std::string& replay_file);

private:
    binance::OrderBook& book_;
    MicrostructureEngine& micro_;
    StrategyEngine& strategies_;
};
