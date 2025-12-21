#include <iostream>
#include <thread>
#include <chrono>
#include <unordered_map>

#include "binance/BinanceSupervisor.hpp"
#include "binance/OrderBook.hpp"

#include "micro/MicrostructureEngine.hpp"
#include "strategy/StrategyEngine.hpp"
#include "execution/ExecutionEngine.hpp"
#include "execution/PositionTracker.hpp"

#include "accounting/PnlLedger.hpp"
#include "accounting/DailyPnlStore.hpp"
#include "risk/RiskManager.hpp"

using namespace std::chrono_literals;

int main() {
    // ----------------------------
    // Accounting + Risk
    // ----------------------------
    PnlLedger pnl;
    DailyPnlStore daily_pnl(0.0);
    RiskManager risk(1000.0);

    // ----------------------------
    // Execution
    // ----------------------------
    PositionTracker positions;
    ExecutionEngine exec(risk, positions);

    // ----------------------------
    // Binance
    // ----------------------------
    binance::BinanceRestClient rest;
    binance::BinanceSupervisor binance(
        rest,
        "logs",
        8081,
        "BINANCE"
    );

    // ----------------------------
    // Build OrderBook* map (FIXED)
    // ----------------------------
    std::unordered_map<std::string, binance::OrderBook*> book_ptrs;
    for (auto& kv : binance.books()) {
        book_ptrs.emplace(
            kv.first,
            const_cast<binance::OrderBook*>(&kv.second)
        );
    }

    // ----------------------------
    // Microstructure
    // ----------------------------
    MicrostructureEngine micro(book_ptrs);

    // ----------------------------
    // Strategies
    // ----------------------------
    StrategyEngine strategies(micro, exec);

    // ----------------------------
    // Start feeds
    // ----------------------------
    binance.start();

    std::cout << "[CHIMERA] running\n";

    while (risk.ok()) {
        micro.update();
        strategies.update();
        std::this_thread::sleep_for(10ms);
    }

    std::cout << "[CHIMERA] risk stop\n";
    return 0;
}
