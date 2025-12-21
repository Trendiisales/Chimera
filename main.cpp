#include "binance/BinanceSupervisor.hpp"
#include "binance/OrderBook.hpp"

#include "micro/MicrostructureEngine.hpp"
#include "strategy/StrategyEngine.hpp"
#include "gui/MetricsHttpServer.hpp"

#include <thread>
#include <chrono>
#include <unordered_map>

int main() {
    // -------------------------------------------------
    // Binance supervisor (self-managed, no lifecycle)
    // -------------------------------------------------
    binance::BinanceRestClient rest;
    binance::BinanceSupervisor binance(
        rest,
        "logs",
        8081,
        "BINANCE"
    );

    // -------------------------------------------------
    // Local order books (engine-main only)
    // -------------------------------------------------
    binance::OrderBook btc_book;
    binance::OrderBook eth_book;

    std::unordered_map<std::string, binance::OrderBook*> books {
        {"BTCUSDT", &btc_book},
        {"ETHUSDT", &eth_book}
    };

    // -------------------------------------------------
    // Microstructure + strategies (no execution yet)
    // -------------------------------------------------
    MicrostructureEngine micro(books);

    // ExecutionEngine intentionally NOT constructed here
    // StrategyEngine will be engine-only (signals only)
    StrategyEngine strategies(micro);

    // -------------------------------------------------
    // GUI
    // -------------------------------------------------
    MetricsHttpServer gui(8080);
    gui.start();

    // -------------------------------------------------
    // Engine loop (pure signal generation)
    // -------------------------------------------------
    while (true) {
        strategies.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
