#pragma once

#include "Strategy.hpp"
#include "ExecutionSink.hpp"

#include <memory>

namespace binance {

class StrategyBridge {
public:
    StrategyBridge(
        std::unique_ptr<Strategy> strategy,
        ExecutionSink& sink
    );

    void on_book(
        const std::string& symbol,
        const OrderBook& book
    );

private:
    std::unique_ptr<Strategy> strategy;
    ExecutionSink& sink;
};

}
