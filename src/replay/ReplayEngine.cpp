#include "replay/ReplayEngine.hpp"
#include "binance/OrderBook.hpp"
#include "micro/MicrostructureEngine.hpp"
#include "strategy/StrategyEngine.hpp"

#include <fstream>
#include <iostream>
#include <vector>

ReplayEngine::ReplayEngine(
    binance::OrderBook& book,
    MicrostructureEngine& micro,
    StrategyEngine& strategies
)
: book_(book), micro_(micro), strategies_(strategies) {}

void ReplayEngine::run(const std::string& replay_file) {
    std::ifstream in(replay_file);
    if (!in.is_open()) {
        std::cerr << "[REPLAY] failed to open " << replay_file << "\n";
        return;
    }

    std::cout << "[REPLAY] running " << replay_file << "\n";

    double bid, ask;

    while (in >> bid >> ask) {
        std::vector<binance::PriceLevel> bids{{bid, 1.0}};
        std::vector<binance::PriceLevel> asks{{ask, 1.0}};

        book_.load_snapshot(bids, asks);

        micro_.update();
        strategies_.update();
    }

    std::cout << "[REPLAY] done\n";
}
