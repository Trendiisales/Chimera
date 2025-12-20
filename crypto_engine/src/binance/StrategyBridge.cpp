#include "binance/StrategyBridge.hpp"

namespace binance {

StrategyBridge::StrategyBridge(
    std::unique_ptr<Strategy> strategy_,
    ExecutionSink& sink_)
    : strategy(std::move(strategy_)),
      sink(sink_) {}

void StrategyBridge::on_book(
    const std::string& symbol,
    const OrderBook& book) {

    ExecutionIntent intent;
    if (!strategy->on_book(symbol, book, intent))
        return;

    sink.on_intent(intent);
}

}
