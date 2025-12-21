#include "binance/OrderBook.hpp"
#include "micro/MicrostructureEngine.hpp"
#include "strategy/StrategyEngine.hpp"
#include "replay/ReplayEngine.hpp"

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: replay <file>\n";
        return 1;
    }

    binance::OrderBook book;
    MicrostructureEngine micro(book);
    StrategyEngine strategies(micro);

    ReplayEngine replay(book, micro, strategies);
    replay.run(argv[1]);

    std::cout << "TOTAL PNL: " << strategies.total_pnl() << "\n";
    for (const auto& kv : strategies.per_strategy_pnl()) {
        std::cout << kv.first << ": " << kv.second << "\n";
    }

    return 0;
}
